#include "address_space.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace MimicOS_NS {

/* Split the VMA at `at` into two pieces: [orig.start, at) and [at, orig.end).
   Inserts the right half and returns iterator to it.  Caller must hold the
   unique lock and must have already validated at strictly between start/end. */
static std::map<uint64_t, Vma>::iterator
split_vma_at(std::map<uint64_t, Vma> &vmas,
             std::map<uint64_t, Vma>::iterator it,
             uint64_t at)
{
    Vma right = it->second;
    right.start = at;
    right.offset += (at - it->second.start);
    it->second.end = at;
    auto [rit, ok] = vmas.insert({at, std::move(right)});
    (void)ok;
    return rit;
}

/* Remove any VMAs or parts of VMAs overlapping [start, end).  Caller holds
   unique lock.  Used by both insert_vma (replace-semantics) and remove_vma. */
static void carve_out(std::map<uint64_t, Vma> &vmas, uint64_t start, uint64_t end)
{
    if (vmas.empty() || end <= start) return;
    /* Find first VMA whose end > start.  Containment for start side:
       upper_bound(start) -> first with key > start; step back. */
    auto it = vmas.upper_bound(start);
    if (it != vmas.begin()) {
        auto prev = std::prev(it);
        if (prev->second.end > start) it = prev;
    }
    while (it != vmas.end() && it->second.start < end) {
        if (it->second.start < start) {
            /* Left overhang: shrink LHS to [start-side). */
            it->second.end = start;
            if (it->second.end <= it->second.start) { it = vmas.erase(it); continue; }
            ++it;
            continue;
        }
        if (it->second.end > end) {
            /* Right overhang: shift RHS to [end, orig_end) and rekey. */
            Vma tail = it->second;
            tail.start = end;
            tail.offset += (end - it->second.start);
            it = vmas.erase(it);
            vmas.insert({end, std::move(tail)});
            break;
        }
        /* Fully contained → drop. */
        it = vmas.erase(it);
    }
}

void AddressSpace::insert_vma(uint64_t start, uint64_t end,
                              int prot, int flags, int fd, uint64_t offset)
{
    if (end <= start) return;
    std::unique_lock lk(m_lock);
    carve_out(m_vmas, start, end);
    Vma v;
    v.start = start;
    v.end = end;
    v.prot = prot;
    v.flags = flags;
    v.fd = fd;
    v.offset = offset;
    auto fit = m_fds.find(fd);
    if (fit != m_fds.end()) v.path = fit->second.path;
    m_vmas.insert({start, std::move(v)});
}

void AddressSpace::remove_vma(uint64_t start, uint64_t end)
{
    if (end <= start) return;
    std::unique_lock lk(m_lock);
    carve_out(m_vmas, start, end);
}

void AddressSpace::mprotect(uint64_t start, uint64_t end, int prot)
{
    if (end <= start) return;
    std::unique_lock lk(m_lock);
    auto it = m_vmas.upper_bound(start);
    if (it != m_vmas.begin()) {
        auto prev = std::prev(it);
        if (prev->second.end > start) it = prev;
    }
    while (it != m_vmas.end() && it->second.start < end) {
        /* Split at `start` if needed. */
        if (it->second.start < start) {
            it = split_vma_at(m_vmas, it, start);
        }
        /* Split at `end` if needed. */
        if (it->second.end > end) {
            split_vma_at(m_vmas, it, end);
        }
        it->second.prot = prot;
        ++it;
    }
}

std::optional<Vma> AddressSpace::vma_containing(uint64_t addr) const
{
    std::shared_lock lk(m_lock);
    auto it = m_vmas.upper_bound(addr);
    if (it == m_vmas.begin()) return std::nullopt;
    --it;
    if (it->second.contains(addr)) return it->second;
    return std::nullopt;
}

size_t AddressSpace::vma_count() const
{
    std::shared_lock lk(m_lock);
    return m_vmas.size();
}

std::vector<Vma> AddressSpace::snapshot_vmas() const
{
    std::shared_lock lk(m_lock);
    std::vector<Vma> out;
    out.reserve(m_vmas.size());
    for (auto &kv : m_vmas) out.push_back(kv.second);
    return out;
}

void AddressSpace::fd_open(int fd, const std::string &path, int flags)
{
    if (fd < 0) return;
    std::unique_lock lk(m_lock);
    m_fds[fd] = FdEntry{path, flags, 0};
}

void AddressSpace::fd_close(int fd)
{
    std::unique_lock lk(m_lock);
    m_fds.erase(fd);
}

std::optional<FdEntry> AddressSpace::fd_lookup(int fd) const
{
    std::shared_lock lk(m_lock);
    auto it = m_fds.find(fd);
    if (it == m_fds.end()) return std::nullopt;
    return it->second;
}

AddressSpaceRegistry& AddressSpaceRegistry::instance()
{
    static AddressSpaceRegistry r;
    return r;
}

AddressSpace* AddressSpaceRegistry::get(int app_id)
{
    std::lock_guard<std::mutex> lk(m_lock);
    auto it = m_spaces.find(app_id);
    if (it != m_spaces.end()) return it->second.get();
    auto as = std::make_unique<AddressSpace>();
    auto *raw = as.get();
    m_spaces.emplace(app_id, std::move(as));
    return raw;
}

void AddressSpaceRegistry::dump(const std::string &path) const
{
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[AddressSpaceRegistry] dump: cannot open " << path << std::endl;
        return;
    }
    std::lock_guard<std::mutex> lk(m_lock);
    for (auto &kv : m_spaces) {
        AddressSpace *as = kv.second.get();
        f << "=== AddressSpace app_id=" << kv.first
          << " vmas=" << as->vma_count() << " ===\n";
        /* Emit each VMA as a single line.  We rely on the AddressSpace's
           own shared_lock inside snapshot_vmas() for a consistent view. */
        auto vmas = as->snapshot_vmas();
        for (const auto &v : vmas) {
            f << "  " << std::hex << v.start << "-" << v.end << std::dec
              << "  size=" << v.size()
              << "  prot=0x" << std::hex << v.prot
              << "  flags=0x" << v.flags << std::dec
              << "  fd=" << v.fd
              << "  off=" << v.offset;
            if (!v.path.empty()) f << "  path=" << v.path;
            f << "\n";
        }
    }
}

} // namespace MimicOS_NS
