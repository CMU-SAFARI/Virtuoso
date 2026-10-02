// tlb_prefetch_sim.cc — replay one PTW dump through a set-associative TLB with a
// PC-conditioned region prefetcher, and count what happens to every prefetched PTE.
//
// Model (matches analyze_dump.py's granularity sweep, Figure 5):
//   * The dump is the stream of demand TLB misses (PTW_Address = VPN, EIP = PC).
//     Each row is replayed as a demand access to the simulated TLB.
//   * Source regions are 2^src_shift pages (32 KB by default). When a PC enters a
//     new source region r, the prefetcher looks up key (r, PC) and predicts the top-k
//     destination regions of 2^shift pages, then inserts every PTE of each predicted
//     region into the TLB. PTEs already resident are not refetched.
//   * Predictor "oracle" uses whole-trace transition counts (exactly Figure 5's
//     top-k tables); "online" learns counts as the trace goes (16 candidates per key,
//     least-frequent replaced).
//   * Only valid PTEs are inserted. A page counts as valid when it appears somewhere
//     in the dump (--ptes valid, the default); --ptes all treats every page in the
//     region as mapped. region_ptes_requested always counts the full 2^shift PTEs per
//     region prefetch, i.e. the page-table memory the walker has to read.
//   * Prefetches are instantaneous (no latency, no bandwidth limit).
//
// Every prefetched PTE ends in exactly one of: used (a later demand access hit it
// before eviction), evicted_unused, or unused_at_end. Accuracy = used / fetched.
//
//   tlb_prefetch_sim --dump <w.csv[.gz]> --workload <w> --out <w.json>
//        [--entries 1536] [--ways 4] [--src-shift 3] [--shifts 0,3,6,9,12] [--ks 4,8]
//        [--predictor oracle|online] [--ptes valid|all] [--filter 0] [--max-rows 0]
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using u64 = uint64_t;

static std::vector<int> parse_list(const std::string& s) {
    std::vector<int> out; std::stringstream ss(s); std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) out.push_back(std::stoi(t));
    return out;
}

struct Trace { std::vector<u64> vpn, pc; };

static Trace load(const std::string& path, u64 max_rows) {
    std::string cmd = (path.size() > 3 && path.compare(path.size() - 3, 3, ".gz") == 0)
                          ? "gzip -dc '" + path + "'" : "cat '" + path + "'";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) throw std::runtime_error("cannot open " + path);
    Trace t; char line[512]; bool header = true;
    while (fgets(line, sizeof line, f)) {
        if (header) { header = false; continue; }   // PTW_Address,EIP,...
        char* end; u64 vpn = strtoull(line, &end, 10);
        if (end == line || *end != ',') continue;
        u64 pc = strtoull(end + 1, &end, 16);
        t.vpn.push_back(vpn); t.pc.push_back(pc);
        if (max_rows && t.vpn.size() >= max_rows) break;
    }
    pclose(f);
    return t;
}

struct Key { u64 region, pc; bool operator==(const Key& o) const { return region == o.region && pc == o.pc; } };
struct KeyHash { size_t operator()(const Key& k) const {
    u64 h = k.region * 0x9E3779B97F4A7C15ull ^ (k.pc + 0x632BE59BD9B4E019ull + (k.region << 6));
    return h ^ (h >> 29);
} };

// ─── TLB ────────────────────────────────────────────────────────────────────
struct Way { u64 vpn = 0, stamp = 0; bool valid = false, pf_unused = false; };

struct Counters {
    u64 demand_accesses = 0, demand_misses = 0;
    u64 bursts = 0, bursts_filtered = 0, bursts_noop = 0, region_ptes_requested = 0;
    u64 pte_fetched = 0, pte_already_resident = 0, pte_used = 0, pte_evicted_unused = 0, pte_unused_at_end = 0;
    // Breakdown of demand accesses by what the prefetcher could know about them:
    //   first     the PC's first access
    //   same      the PC stays in its current source region (no transition, never predicted)
    //   cov       a transition whose destination region is in the top-k issued on entry
    //   uncov     a transition whose destination is not in the top-k
    // *_hit counts TLB hits in each class. Covered transitions are also bucketed by the gap
    // (demand accesses) between the prefetch, issued when the PC entered the source region,
    // and the transition that uses it: <=16, <=64, <=256, <=1024, >1024.
    u64 acc_first = 0, acc_same = 0, acc_cov = 0, acc_uncov = 0;
    u64 hit_first = 0, hit_same = 0, hit_cov = 0, hit_uncov = 0;
    u64 gap_n[5] = {}, gap_hit[5] = {};
};

struct TLB {
    u64 sets, ways, clock = 0; int shift;          // shift: region size for resident counts
    std::vector<Way> w;
    std::unordered_map<u64, u64> resident;          // region -> resident PTE count
    Counters* c;
    TLB(u64 entries, u64 ways_, int shift_, Counters* c_) : sets(entries / ways_), ways(ways_), shift(shift_), w(entries), c(c_) {}
    Way* set(u64 vpn) { return &w[(vpn % sets) * ways]; }
    Way* find(u64 vpn) { Way* s = set(vpn); for (u64 i = 0; i < ways; ++i) if (s[i].valid && s[i].vpn == vpn) return &s[i]; return nullptr; }
    void fill(u64 vpn, bool pf) {
        Way* s = set(vpn); Way* v = &s[0];
        for (u64 i = 0; i < ways; ++i) { if (!s[i].valid) { v = &s[i]; break; } if (s[i].stamp < v->stamp) v = &s[i]; }
        if (v->valid) {
            if (v->pf_unused) ++c->pte_evicted_unused;
            auto it = resident.find(v->vpn >> shift); if (--it->second == 0) resident.erase(it);
        }
        *v = {vpn, ++clock, true, pf};
        ++resident[vpn >> shift];
    }
    bool demand(u64 vpn) {
        ++c->demand_accesses;
        if (Way* h = find(vpn)) { if (h->pf_unused) { ++c->pte_used; h->pf_unused = false; } h->stamp = ++clock; return true; }
        ++c->demand_misses; fill(vpn, false); return false;
    }
    // Prefetch one PTE of a region burst. turned[set] counts fresh fills of this burst in
    // that set: once it reaches `ways`, the set holds only this burst's unused PTEs (all of
    // the same region), so a further PTE cannot be resident and just replaces the LRU one.
    // That path skips the lookup and the per-region resident count, which does not change.
    void prefetch(u64 vpn, std::vector<uint8_t>& turned) {
        u64 si = vpn % sets;
        if (turned[si] >= ways) {
            Way* s = &w[si * ways]; Way* v = &s[0];
            for (u64 i = 1; i < ways; ++i) if (s[i].stamp < v->stamp) v = &s[i];
            ++c->pte_fetched; ++c->pte_evicted_unused; *v = {vpn, ++clock, true, true};
            return;
        }
        if (find(vpn)) { ++c->pte_already_resident; return; }
        ++c->pte_fetched; fill(vpn, true); ++turned[si];
    }
    void finish() { for (auto& x : w) if (x.valid && x.pf_unused) ++c->pte_unused_at_end; }
};

// ─── predictors ─────────────────────────────────────────────────────────────
struct Predictor {
    virtual ~Predictor() = default;
    virtual void learn(const Key& k, u64 dst) = 0;
    virtual void predict(const Key& k, std::vector<u64>& out) = 0;
};

struct Oracle : Predictor {   // whole-trace top-k, as in analyze_dump.py
    std::unordered_map<Key, std::vector<u64>, KeyHash> top;
    Oracle(const Trace& t, int src_shift, int shift, int k) {
        std::unordered_map<Key, std::unordered_map<u64, u64>, KeyHash> cnt;
        std::unordered_map<u64, u64> prev;
        for (size_t i = 0; i < t.vpn.size(); ++i) {
            u64 r = t.vpn[i] >> src_shift; auto p = prev.find(t.pc[i]);
            if (p != prev.end() && p->second != r) ++cnt[{p->second, t.pc[i]}][t.vpn[i] >> shift];
            prev[t.pc[i]] = r;
        }
        for (auto& [key, dd] : cnt) {
            std::vector<std::pair<u64, u64>> v(dd.begin(), dd.end());   // (dst, count)
            size_t n = std::min<size_t>(k, v.size());
            std::partial_sort(v.begin(), v.begin() + n, v.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
            auto& o = top[key]; for (size_t i = 0; i < n; ++i) o.push_back(v[i].first);
        }
    }
    void learn(const Key&, u64) override {}
    void predict(const Key& k, std::vector<u64>& out) override { auto it = top.find(k); if (it != top.end()) out = it->second; }
};

struct Online : Predictor {   // learned as the trace goes; 16 LFU candidates per key
    static constexpr size_t CAP = 16;
    int k; std::unordered_map<Key, std::vector<std::pair<u64, u64>>, KeyHash> t;
    explicit Online(int k_) : k(k_) {}
    void learn(const Key& key, u64 dst) override {
        auto& v = t[key];
        for (auto& e : v) if (e.first == dst) { ++e.second; return; }
        if (v.size() < CAP) { v.push_back({dst, 1}); return; }
        auto m = std::min_element(v.begin(), v.end(), [](auto& a, auto& b) { return a.second < b.second; });
        *m = {dst, 1};
    }
    void predict(const Key& key, std::vector<u64>& out) override {
        auto it = t.find(key); if (it == t.end()) return;
        auto v = it->second; size_t n = std::min<size_t>(k, v.size());
        std::partial_sort(v.begin(), v.begin() + n, v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < n; ++i) out.push_back(v[i].first);
    }
};

// ─── one configuration ──────────────────────────────────────────────────────
struct Cfg { u64 entries, ways; int src_shift, shift, k; bool online, all_ptes; size_t filter; };

static Counters run(const Trace& t, const std::vector<u64>& valid, const Cfg& cfg, bool prefetch) {
    Counters c; TLB tlb(cfg.entries, cfg.ways, cfg.shift, &c);
    Predictor* pred = nullptr;
    if (prefetch) pred = cfg.online ? static_cast<Predictor*>(new Online(cfg.k)) : new Oracle(t, cfg.src_shift, cfg.shift, cfg.k);
    std::unordered_map<u64, u64> prev, entry;   // per PC: current source region, index where it entered it
    std::vector<u64> recent(cfg.filter, ~0ull); size_t head = 0;   // recently prefetched regions (FIFO)
    std::vector<u64> dst; std::vector<uint8_t> turned(cfg.entries / cfg.ways);
    for (size_t i = 0; i < t.vpn.size(); ++i) {
        u64 vpn = t.vpn[i], pc = t.pc[i], r = vpn >> cfg.src_shift;
        bool hit = tlb.demand(vpn);
        auto p = prev.find(pc); bool entered = (p == prev.end() || p->second != r);
        if (p == prev.end()) { ++c.acc_first; c.hit_first += hit; }
        else if (!entered) { ++c.acc_same; c.hit_same += hit; }
        else if (prefetch) {
            dst.clear(); pred->predict({p->second, pc}, dst);
            if (std::find(dst.begin(), dst.end(), vpn >> cfg.shift) != dst.end()) {
                ++c.acc_cov; c.hit_cov += hit;
                u64 gap = i - entry[pc]; int b = gap <= 16 ? 0 : gap <= 64 ? 1 : gap <= 256 ? 2 : gap <= 1024 ? 3 : 4;
                ++c.gap_n[b]; c.gap_hit[b] += hit;
            } else { ++c.acc_uncov; c.hit_uncov += hit; }
        } else { ++c.acc_uncov; c.hit_uncov += hit; }
        if (entered) entry[pc] = i;
        if (!prefetch) { prev[pc] = r; continue; }
        if (p != prev.end() && p->second != r) pred->learn({p->second, pc}, vpn >> cfg.shift);
        prev[pc] = r;
        if (!entered) continue;
        dst.clear(); pred->predict({r, pc}, dst);
        for (u64 R : dst) {
            if (cfg.filter && std::find(recent.begin(), recent.end(), R) != recent.end()) { ++c.bursts_filtered; continue; }
            if (cfg.filter) { recent[head] = R; head = (head + 1) % cfg.filter; }
            ++c.bursts; c.region_ptes_requested += 1ull << cfg.shift;
            std::fill(turned.begin(), turned.end(), 0);
            u64 lo = R << cfg.shift, hi = (R + 1) << cfg.shift;
            if (cfg.all_ptes) {
                auto it = tlb.resident.find(R);
                if (it != tlb.resident.end() && it->second == (hi - lo)) { ++c.bursts_noop; c.pte_already_resident += hi - lo; continue; }
                for (u64 v = lo; v < hi; ++v) tlb.prefetch(v, turned);
            } else {
                auto a = std::lower_bound(valid.begin(), valid.end(), lo), b = std::lower_bound(a, valid.end(), hi);
                u64 n = b - a; auto it = tlb.resident.find(R);
                if (it != tlb.resident.end() && it->second == n) { ++c.bursts_noop; c.pte_already_resident += n; continue; }
                for (; a != b; ++a) tlb.prefetch(*a, turned);
            }
        }
    }
    tlb.finish(); delete pred;
    return c;
}

static void emit(std::ostream& o, const char* name, u64 v, bool comma = true) { o << "\"" << name << "\": " << v << (comma ? ", " : ""); }

int main(int argc, char** argv) {
    std::string dump, workload, out, shifts_s = "0,3,6,9,12", ks_s = "4,8", predictor = "oracle", ptes = "valid";
    u64 entries = 1536, ways = 4, max_rows = 0; int src_shift = 3; size_t filter = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string a = argv[i], v = argv[i + 1];
        if (a == "--dump") dump = v; else if (a == "--workload") workload = v; else if (a == "--out") out = v;
        else if (a == "--entries") entries = std::stoull(v); else if (a == "--ways") ways = std::stoull(v);
        else if (a == "--src-shift") src_shift = std::stoi(v); else if (a == "--shifts") shifts_s = v;
        else if (a == "--ks") ks_s = v; else if (a == "--predictor") predictor = v; else if (a == "--ptes") ptes = v;
        else if (a == "--filter") filter = std::stoul(v); else if (a == "--max-rows") max_rows = std::stoull(v);
        else { std::cerr << "unknown option " << a << "\n"; return 2; }
    }
    if (dump.empty() || workload.empty() || out.empty() || entries % ways) {
        std::cerr << "usage: tlb_prefetch_sim --dump <csv[.gz]> --workload <name> --out <json> [options]\n"; return 2;
    }
    auto t0 = std::chrono::steady_clock::now();
    Trace t = load(dump, max_rows);
    std::vector<u64> valid(t.vpn); std::sort(valid.begin(), valid.end()); valid.erase(std::unique(valid.begin(), valid.end()), valid.end());

    std::ostringstream o;
    o << "{\"workload\": \"" << workload << "\", \"rows\": " << t.vpn.size() << ", \"unique_vpns\": " << valid.size()
      << ", \"entries\": " << entries << ", \"ways\": " << ways << ", \"src_shift\": " << src_shift
      << ", \"predictor\": \"" << predictor << "\", \"ptes\": \"" << ptes << "\", \"filter\": " << filter;
    Cfg base{entries, ways, src_shift, 0, 0, predictor == "online", ptes == "all", filter};
    Counters b = run(t, valid, base, false);
    o << ", \"baseline_misses\": " << b.demand_misses << ", \"baseline_acc\": [" << b.acc_first << ", " << b.acc_same << ", " << b.acc_uncov << "]"
      << ", \"baseline_hit\": [" << b.hit_first << ", " << b.hit_same << ", " << b.hit_uncov << "], \"results\": [";
    bool first = true;
    for (int sh : parse_list(shifts_s)) for (int k : parse_list(ks_s)) {
        Cfg cfg = base; cfg.shift = sh; cfg.k = k;
        auto s0 = std::chrono::steady_clock::now();
        Counters c = run(t, valid, cfg, true);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - s0).count();
        if (c.pte_fetched != c.pte_used + c.pte_evicted_unused + c.pte_unused_at_end) { std::cerr << "accounting mismatch\n"; return 1; }
        o << (first ? "" : ", ") << "{"; first = false;
        emit(o, "shift", sh); emit(o, "k", k);
        emit(o, "demand_misses", c.demand_misses); emit(o, "bursts", c.bursts); emit(o, "bursts_filtered", c.bursts_filtered);
        emit(o, "bursts_noop", c.bursts_noop); emit(o, "region_ptes_requested", c.region_ptes_requested);
        emit(o, "pte_fetched", c.pte_fetched); emit(o, "pte_already_resident", c.pte_already_resident);
        emit(o, "pte_used", c.pte_used); emit(o, "pte_evicted_unused", c.pte_evicted_unused);
        emit(o, "pte_unused_at_end", c.pte_unused_at_end);
        emit(o, "acc_first", c.acc_first); emit(o, "acc_same", c.acc_same); emit(o, "acc_cov", c.acc_cov); emit(o, "acc_uncov", c.acc_uncov);
        emit(o, "hit_first", c.hit_first); emit(o, "hit_same", c.hit_same); emit(o, "hit_cov", c.hit_cov); emit(o, "hit_uncov", c.hit_uncov);
        o << "\"gap_n\": [" << c.gap_n[0] << ", " << c.gap_n[1] << ", " << c.gap_n[2] << ", " << c.gap_n[3] << ", " << c.gap_n[4] << "], ";
        o << "\"gap_hit\": [" << c.gap_hit[0] << ", " << c.gap_hit[1] << ", " << c.gap_hit[2] << ", " << c.gap_hit[3] << ", " << c.gap_hit[4] << "], ";
        o << "\"seconds\": " << secs << "}";
        std::cerr << workload << " shift=" << sh << " k=" << k << " fetched=" << c.pte_fetched << " used=" << c.pte_used
                  << " evicted_unused=" << c.pte_evicted_unused << " misses=" << c.demand_misses << "/" << b.demand_misses
                  << " (" << secs << " s)\n";
    }
    o << "], \"seconds\": " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << "}\n";
    std::ofstream(out) << o.str();
    return 0;
}
