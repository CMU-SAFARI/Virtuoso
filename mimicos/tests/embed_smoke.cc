/**
 * @file embed_smoke.cc
 * @brief Standalone exercise of the MimicOS embedding API.
 *
 * Deliberately links only against libmimicos — no Sniper, no ChampSim, no
 * gem5 — so a failure here is unambiguously a library bug rather than an
 * adapter bug.  Build:  make -C mimicos test-embed && mimicos/build/embed_smoke
 */
#include "mimicos_embed.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);                \
            std::printf(__VA_ARGS__);                                         \
            std::printf("\n");                                                \
            g_failures++;                                                     \
        }                                                                     \
    } while (0)

static mimicos::KernelConfig base_config()
{
    mimicos::KernelConfig cfg;
    cfg.allocator           = "baseline";
    cfg.memory_size_mb      = 4096;
    cfg.kernel_size_mb      = 512;
    cfg.max_order           = 12;
    cfg.fragmentation_type  = "largepage";
    cfg.fragmentation       = 0.0;
    cfg.pt_levels           = 4;
    /* Non-zero calibration so latency assertions are meaningful. */
    cfg.vma_lookup_base_ns      = 100;
    cfg.pt_page_alloc_cost_ns   = 200;
    cfg.phys_page_alloc_cost_ns = 400;
    cfg.zero_fill_cost_ns       = 800;
    cfg.pte_install_cost_ns     = 50;
    return cfg;
}

static mimicos::FaultRequest req_for(uint64_t va, uint32_t asid = 0)
{
    mimicos::FaultRequest r;
    r.vaddr = va;
    r.asid  = asid;
    return r;
}

/* ---------------------------------------------------------------- */

static void test_basic_translate()
{
    std::printf("[test] basic translate + repeat\n");
    std::string err;
    auto k = mimicos::Kernel::create(base_config(), &err);
    CHECK(k != nullptr, "kernel creation failed: %s", err.c_str());
    if (!k) return;

    const uint64_t va = 0x7f0000001234ull;
    auto t1 = k->translate(req_for(va));
    CHECK(t1.ok, "first translate failed");
    CHECK(t1.faulted, "first translate should fault");
    CHECK(t1.fault_latency_ns > 0, "faulting translate should carry a cost");

    auto t2 = k->translate(req_for(va));
    CHECK(t2.ok, "second translate failed");
    CHECK(!t2.faulted, "second translate must hit, not fault");
    CHECK(t2.fault_latency_ns == 0, "hit must be free");
    CHECK(t1.ppn == t2.ppn, "translate is not stable: %lu vs %lu",
          (unsigned long)t1.ppn, (unsigned long)t2.ppn);

    /* A different offset in the same 4 KiB page maps to the same frame. */
    auto t3 = k->translate(req_for(va + 0x10));
    CHECK(t3.ppn == t1.ppn, "same page mapped to different frames");

    auto st = k->stats();
    CHECK(st.minor_faults == 1, "expected exactly 1 minor fault, got %lu",
          (unsigned long)st.minor_faults);
    CHECK(st.translations == 3, "expected 3 translations, got %lu",
          (unsigned long)st.translations);
}

static void test_probe_does_not_fault()
{
    std::printf("[test] probe never allocates\n");
    std::string err;
    auto k = mimicos::Kernel::create(base_config(), &err);
    if (!k) { CHECK(false, "kernel creation failed: %s", err.c_str()); return; }

    const uint64_t va = 0x400000ull;
    uint64_t ppn = 0; uint32_t ps = 0;
    CHECK(!k->probe(0, va, &ppn, &ps), "probe of unmapped VA must fail");
    CHECK(k->stats().minor_faults == 0, "probe caused a fault");

    k->translate(req_for(va));
    CHECK(k->probe(0, va, &ppn, &ps), "probe after translate must succeed");
    CHECK(k->stats().minor_faults == 1, "probe added an extra fault");
}

static void test_walk_shape()
{
    std::printf("[test] walk returns one PTE address per level\n");
    std::string err;
    auto k = mimicos::Kernel::create(base_config(), &err);
    if (!k) { CHECK(false, "kernel creation failed: %s", err.c_str()); return; }

    const uint64_t va = 0x123456789000ull;
    k->translate(req_for(va));

    auto steps = k->walk(req_for(va));
    CHECK(steps.size() == 4, "expected 4 walk steps for a 4-level 4 KiB walk, got %zu",
          steps.size());
    if (steps.size() == 4) {
        for (size_t i = 0; i < steps.size(); i++) {
            CHECK(steps[i].level == 4 - i, "step %zu has level %u", i, steps[i].level);
            CHECK(steps[i].depth == i, "step %zu has depth %u", i, steps[i].depth);
            CHECK(steps[i].pte_paddr % 8 == 0, "PTE address %lu is not 8-byte aligned",
                  (unsigned long)steps[i].pte_paddr);
        }
        CHECK(!steps[0].is_leaf, "root entry must not be a leaf");
        CHECK(steps[3].is_leaf, "last entry must be the leaf");
    }

    /* All four PTE addresses must be distinct frames. */
    for (size_t i = 1; i < steps.size(); i++) {
        CHECK(steps[i].pte_paddr / 4096 != steps[i - 1].pte_paddr / 4096,
              "levels %zu and %zu share a page-table frame", i - 1, i);
    }

    CHECK(k->leaf_level(0, va) == 1, "4 KiB page should report leaf level 1, got %u",
          k->leaf_level(0, va));
    CHECK(k->root_table_paddr(0) % 4096 == 0, "CR3 must be frame-aligned");
}

static void test_pte_addresses_match_index_bits()
{
    std::printf("[test] PTE address encodes the right index bits\n");
    std::string err;
    auto k = mimicos::Kernel::create(base_config(), &err);
    if (!k) { CHECK(false, "kernel creation failed: %s", err.c_str()); return; }

    const uint64_t va = 0x0000'5566'7788'9000ull;
    k->translate(req_for(va));
    auto steps = k->walk(req_for(va));
    if (steps.size() != 4) { CHECK(false, "unexpected walk length %zu", steps.size()); return; }

    const uint32_t expect[4] = {
        static_cast<uint32_t>((va >> 39) & 0x1ff),
        static_cast<uint32_t>((va >> 30) & 0x1ff),
        static_cast<uint32_t>((va >> 21) & 0x1ff),
        static_cast<uint32_t>((va >> 12) & 0x1ff),
    };
    for (int i = 0; i < 4; i++) {
        uint32_t idx = static_cast<uint32_t>((steps[i].pte_paddr % 4096) / 8);
        CHECK(idx == expect[i], "depth %d: index %u != expected %u", i, idx, expect[i]);
    }
}

static void test_address_space_isolation()
{
    std::printf("[test] distinct ASIDs get distinct page tables\n");
    std::string err;
    auto k = mimicos::Kernel::create(base_config(), &err);
    if (!k) { CHECK(false, "kernel creation failed: %s", err.c_str()); return; }

    const uint64_t va = 0x600000ull;
    auto a = k->translate(req_for(va, /*asid=*/0));
    auto b = k->translate(req_for(va, /*asid=*/1));
    CHECK(a.ok && b.ok, "translations failed");
    CHECK(a.ppn != b.ppn, "same VA in two address spaces aliased to one frame");
    CHECK(k->root_table_paddr(0) != k->root_table_paddr(1),
          "two address spaces share a root table");

    uint64_t ppn = 0; uint32_t ps = 0;
    CHECK(k->probe(1, va, &ppn, &ps) && ppn == b.ppn, "asid 1 probe mismatch");
}

static void test_huge_pages()
{
    std::printf("[test] reserve_thp promotion yields 2 MiB pages and a shorter walk\n");
    auto cfg = base_config();
    cfg.allocator           = "reserve_thp";
    /* ReserveTHP promotes a reserved 2 MiB region once its 4 KiB utilisation
       reaches this fraction, so a low threshold lets a short scan trigger it. */
    cfg.promotion_threshold = 0;
    std::string err;
    auto k = mimicos::Kernel::create(cfg, &err);
    if (!k) { CHECK(false, "kernel creation failed: %s", err.c_str()); return; }

    const uint64_t base = 0x200000000ull;
    /* Touch pages within one 2 MiB region until the allocator promotes it.
       This is the promotion path that used to break install(): the PMD entry
       must become a huge leaf on top of an existing 4 KiB subtree. */
    mimicos::Translation t;
    unsigned touched = 0;
    for (; touched < 512; touched++) {
        t = k->translate(req_for(base + touched * 4096ull));
        CHECK(t.ok, "translate #%u failed (promotion path broken?)", touched);
        if (!t.ok) return;
        if (t.page_size_bits == 21)
            break;
    }
    const uint64_t va = base + touched * 4096ull;
    std::printf("       promoted after %u touches: page_size_bits=%u leaf_level=%u\n",
                touched, t.page_size_bits, k->leaf_level(0, va));

    if (t.page_size_bits == 21) {
        CHECK(k->leaf_level(0, va) == 2, "2 MiB page must report leaf level 2, got %u",
              k->leaf_level(0, va));
        auto steps = k->walk(req_for(va));
        CHECK(steps.size() == 3, "2 MiB walk should be 3 steps, got %zu", steps.size());
        if (steps.size() == 3)
            CHECK(steps[2].is_leaf, "PD entry must be the leaf after promotion");

        /* Every 4 KiB page inside the huge frame resolves within it, and the
           huge frame's base is 2 MiB-aligned. */
        const uint64_t huge_base_va = va & ~0x1fffffull;
        uint64_t base_ppn = 0; uint32_t base_ps = 0;
        CHECK(k->probe(0, huge_base_va, &base_ppn, &base_ps), "huge base probe failed");
        CHECK(base_ps == 21, "huge base reports page size %u", base_ps);
        CHECK(base_ppn % 512 == 0, "2 MiB frame base ppn %lu is not 2 MiB-aligned",
              (unsigned long)base_ppn);

        for (uint64_t off = 0; off < 0x200000ull; off += 0x40000ull) {
            uint64_t ppn = 0; uint32_t ps = 0;
            CHECK(k->probe(0, huge_base_va + off, &ppn, &ps),
                  "probe inside 2 MiB page failed at offset 0x%lx", (unsigned long)off);
            CHECK(ppn == base_ppn + (off >> 12),
                  "offset 0x%lx inside 2 MiB frame mis-resolved: %lu vs %lu",
                  (unsigned long)off, (unsigned long)ppn,
                  (unsigned long)(base_ppn + (off >> 12)));
        }

        /* An untouched page inside the promoted region must now hit, not fault. */
        const uint64_t untouched = huge_base_va + 0x1ff000ull;
        auto u = k->translate(req_for(untouched));
        CHECK(u.ok && !u.faulted,
              "access inside an existing 2 MiB mapping must not fault");
    } else {
        std::printf("       (allocator never promoted; huge-page assertions skipped)\n");
    }
}

/** The promotion path that install() used to reject: a 2 MiB leaf has to be
 *  installed on top of an already-populated 4 KiB subtree. */
static void test_promotion_over_existing_subtree()
{
    std::printf("[test] promotion on top of an existing 4 KiB subtree\n");
    auto cfg = base_config();
    cfg.allocator           = "reserve_thp";
    cfg.promotion_threshold = 0.5;   /* needs ~256 touched pages */
    std::string err;
    auto k = mimicos::Kernel::create(cfg, &err);
    if (!k) { CHECK(false, "kernel creation failed: %s", err.c_str()); return; }

    const uint64_t base = 0x300000000ull;
    unsigned promote_at = 0;
    bool saw_4k = false, promoted = false;

    for (unsigned i = 0; i < 512; i++) {
        auto t = k->translate(req_for(base + i * 4096ull));
        CHECK(t.ok, "translate #%u failed", i);
        if (!t.ok) return;
        if (t.page_size_bits == 12) saw_4k = true;
        if (t.page_size_bits == 21 && !promoted) { promoted = true; promote_at = i; }
    }

    CHECK(saw_4k, "expected 4 KiB mappings before promotion with threshold 0.5");
    CHECK(promoted, "region never promoted at threshold 0.5");
    if (!promoted) return;
    std::printf("       4 KiB mappings for %u pages, then promoted\n", promote_at);
    CHECK(promote_at > 0, "promotion happened immediately; subtree path not exercised");

    /* After promotion the whole region must resolve through the huge leaf, and
       every 4 KiB page must still land on the right frame. */
    CHECK(k->leaf_level(0, base) == 2, "post-promotion leaf level is %u",
          k->leaf_level(0, base));

    uint64_t huge_ppn = 0; uint32_t ps = 0;
    CHECK(k->probe(0, base, &huge_ppn, &ps), "probe of promoted base failed");
    CHECK(ps == 21, "promoted base reports page size %u", ps);

    for (unsigned i = 0; i < 512; i += 37) {
        uint64_t ppn = 0; uint32_t p = 0;
        CHECK(k->probe(0, base + i * 4096ull, &ppn, &p),
              "probe of page %u in promoted region failed", i);
        CHECK(ppn == huge_ppn + i,
              "page %u resolves to %lu, expected %lu", i,
              (unsigned long)ppn, (unsigned long)(huge_ppn + i));
    }

    /* Promotion must not double-count the frames it subsumes. */
    auto st = k->stats();
    CHECK(st.data_frames_allocated <= 1024,
          "promotion double-counted frames: data_frames_allocated=%lu",
          (unsigned long)st.data_frames_allocated);
    CHECK(st.huge_page_faults > 0, "no huge-page fault recorded");
    /* Promotion reuses the frames it covers, and must not be reported. */
    CHECK(st.aliased_frames == 0,
          "promotion was mistaken for aliasing: %lu frames flagged",
          (unsigned long)st.aliased_frames);
}

/** Distinct virtual pages must never share a physical frame.
 *
 *  This is the failure mode that would make MimicOS look *good* for the wrong
 *  reason: aliasing several VPNs onto one frame shrinks the working set and
 *  drops cache misses, which is indistinguishable from better placement unless
 *  it is checked directly. */
static void test_frames_are_not_aliased()
{
    std::printf("[test] distinct virtual pages never share a frame\n");
    const char* allocators[] = {"baseline", "reserve_thp", "linux_buddy_anon"};

    for (const char* alloc : allocators) {
        auto cfg = base_config();
        cfg.allocator = alloc;
        cfg.promotion_threshold = 0.5;
        std::string err;
        auto k = mimicos::Kernel::create(cfg, &err);
        if (!k) { CHECK(false, "kernel creation failed for %s: %s", alloc, err.c_str()); continue; }

        /* ppn -> vpn that claimed it */
        std::map<uint64_t, uint64_t> owner;
        unsigned collisions = 0;

        /* Three regions with different strides, so sequential, strided and
           sparse allocation patterns are all covered. */
        const uint64_t bases[] = {0x100000000ull, 0x7f0000000000ull, 0x2000000ull};
        const uint64_t strides[] = {4096ull, 4096ull * 7, 4096ull * 512};

        for (int r = 0; r < 3; r++) {
            for (unsigned i = 0; i < 600; i++) {
                const uint64_t va = bases[r] + i * strides[r];
                auto t = k->translate(req_for(va));
                CHECK(t.ok, "%s: translate failed at region %d page %u", alloc, r, i);
                if (!t.ok) break;

                const uint64_t vpn = va >> 12;
                auto it = owner.find(t.ppn);
                if (it != owner.end() && it->second != vpn) {
                    if (collisions < 3) {
                        std::printf("  FAIL %s: ppn %lu claimed by vpn %lu and vpn %lu\n",
                                    alloc, (unsigned long)t.ppn,
                                    (unsigned long)it->second, (unsigned long)vpn);
                    }
                    collisions++;
                } else {
                    owner[t.ppn] = vpn;
                }
            }
        }

        /* The library's own guard must agree with what we counted. */
        const auto st = k->stats();
        CHECK(st.aliased_frames == 0, "%s: library reported %lu aliased frames",
              alloc, (unsigned long)st.aliased_frames);

        if (collisions > 0) {
            g_failures++;
            std::printf("  FAIL %s: %u aliased frames\n", alloc, collisions);
        } else {
            std::printf("       %-18s %zu distinct frames, no aliasing\n", alloc, owner.size());
        }
    }
}

static void test_fault_cost_model()
{
    std::printf("[test] calibrated fault cost is applied\n");
    auto cfg = base_config();
    std::string err;
    auto k = mimicos::Kernel::create(cfg, &err);
    if (!k) { CHECK(false, "kernel creation failed: %s", err.c_str()); return; }

    auto t = k->translate(req_for(0x800000ull));
    /* vma_lookup + pt_alloc + phys_alloc + zero_fill + pte_install */
    const uint64_t expect = 100 + 200 + 400 + 800 + 50;
    CHECK(t.fault_latency_ns == expect, "fault cost %lu != expected %lu",
          (unsigned long)t.fault_latency_ns, (unsigned long)expect);

    /* Second fault in the same PMD reuses interior tables: no pt_alloc charge. */
    auto t2 = k->translate(req_for(0x801000ull));
    const uint64_t expect2 = 100 + 400 + 800 + 50;
    CHECK(t2.fault_latency_ns == expect2,
          "second fault cost %lu != expected %lu (interior tables should be reused)",
          (unsigned long)t2.fault_latency_ns, (unsigned long)expect2);
}

static void test_config_rejects_bad_input()
{
    std::printf("[test] invalid configs are rejected, not silently accepted\n");
    std::string err;

    auto bad_levels = base_config();
    bad_levels.pt_levels = 7;
    CHECK(mimicos::Kernel::create(bad_levels, &err) == nullptr, "pt_levels=7 accepted");

    auto bad_alloc = base_config();
    bad_alloc.allocator = "does_not_exist";
    CHECK(mimicos::Kernel::create(bad_alloc, &err) == nullptr, "bogus allocator accepted");

    auto bad_mem = base_config();
    bad_mem.kernel_size_mb = bad_mem.memory_size_mb;
    CHECK(mimicos::Kernel::create(bad_mem, &err) == nullptr, "kernel >= memory accepted");

    mimicos::KernelConfig parsed;
    CHECK(!mimicos::KernelConfig::from_ini("/nonexistent/mimicos.ini", &parsed, &err),
          "from_ini accepted a missing file");
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    std::printf("=== libmimicos embedding smoke test ===\n");

    test_basic_translate();
    test_probe_does_not_fault();
    test_walk_shape();
    test_pte_addresses_match_index_bits();
    test_address_space_isolation();
    test_huge_pages();
    test_promotion_over_existing_subtree();
    test_frames_are_not_aliased();
    test_fault_cost_model();
    test_config_rejects_bad_input();

    if (g_failures == 0) {
        std::printf("=== all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::printf("=== %d check(s) FAILED ===\n", g_failures);
    return EXIT_FAILURE;
}
