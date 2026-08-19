/*
 * @kanellok: the page table a host simulator can actually walk.
 *
 * Radix page table without the Sniper couplings.
 *
 * Ported from PageTableRadix in Sniper, minus SimLog, Core*,
 * SubsecondTime, the page walk cache and the stats registry.  What is
 * left is the part a host simulator needs: a real tree whose interior
 * frames come from the MimicOS allocator, so a walk yields physical
 * entry addresses the host can actually fetch.
 *
 * x86-64 geometry: 9 index bits per level, 8-byte entries, 512 per 4K
 * frame.  Large pages end the walk early.
 */
#pragma once

#include <cstdint>
#include <cstddef>

class PhysicalMemoryAllocator;

namespace mimicos {

class EmbedRadixPageTable {
public:
    /* levels is 4 for x86-64, 5 for LA57.  512 entries per 4K frame. */
    EmbedRadixPageTable(PhysicalMemoryAllocator* alloc, int levels,
                        int entries_per_frame = 512);
    ~EmbedRadixPageTable();

    EmbedRadixPageTable(const EmbedRadixPageTable&)            = delete;
    EmbedRadixPageTable& operator=(const EmbedRadixPageTable&) = delete;

    /* Frame holding the root table. */
    uint64_t root_ppn() const { return m_root->ppn; }

    /*
     * Resolve va.  *ppn is the base frame of the mapped page, not yet
     * offset into a large one.  False if there is no leaf entry.
     */
    bool lookup(uint64_t va, uint64_t* ppn, uint32_t* page_size_bits) const;

    /*
     * Install a leaf, allocating any missing interior frames.  Returns
     * how many it allocated, or -1 on failure.
     *
     * Handles promotion: when the allocator upgrades a region that
     * already has 4K mappings, the interior entry becomes a large leaf
     * and its subtree is kept, so a later demotion can restore them.
     * Sniper's version overwrites a union member here and leaks the
     * subtree.  promoted says whether that happened, since those frames
     * were already accounted for.
     */
    int install(uint64_t va, uint64_t ppn, uint32_t page_size_bits,
                bool* promoted = nullptr);

    /* Drop the leaf covering va.  Interior frames stay, for demotion. */
    void unmap(uint64_t va);

    /*
     * Address of the entry read at depth (0 is the root) on the walk for
     * va.  With allocate set, missing tables above depth are created.
     * False if the depth cannot be reached.
     */
    bool entry_address(uint64_t va, uint32_t depth, bool allocate,
                       uint64_t* pte_paddr, bool* is_leaf,
                       uint32_t* frames_allocated = nullptr);

    /* Depth of the leaf covering va; levels - 1 when unmapped. */
    uint32_t leaf_depth(uint64_t va) const;

    int      levels() const { return m_levels; }
    uint64_t frames_allocated() const { return m_frames_allocated; }

    /* Index into the table at depth. */
    uint32_t index_at(uint64_t va, uint32_t depth) const {
        return static_cast<uint32_t>((va >> shift_at(depth)) & (m_entries - 1));
    }
    uint32_t shift_at(uint32_t depth) const {
        return static_cast<uint32_t>(12 + 9 * (m_levels - 1 - static_cast<int>(depth)));
    }

    /* Depth at which a page of this size puts its leaf. */
    uint32_t leaf_depth_for_size(uint32_t page_size_bits) const {
        return static_cast<uint32_t>(m_levels - 1) - ((page_size_bits - 12) / 9);
    }

private:
    struct Frame;

    struct Entry {
        bool is_pte = false;   ///< leaf entry (holds a translation)
        bool valid  = false;   ///< leaf: translation present / interior: unused
        uint64_t ppn = 0;      ///< leaf: mapped PPN
        uint32_t page_size_bits = 12;
        Frame*   next = nullptr; ///< interior: child table
    };

    struct Frame {
        Entry*   entries = nullptr;
        uint64_t ppn     = 0;   ///< emulated physical frame backing this table
    };

    Frame* new_frame();
    void   free_frame(Frame* f, int depth);

    PhysicalMemoryAllocator* m_alloc;
    Frame*   m_root = nullptr;
    int      m_levels;
    int      m_entries;
    uint64_t m_frames_allocated = 0;
    /* x86-64 entries are 8 bytes. */
    static constexpr uint64_t kEntryBytes = 8;
    static constexpr uint64_t kFrameBytes = 4096;
};

} // namespace mimicos
