#include "embed/radix_pt_embed.h"

#include "fixed_types.h"
#include "memory_management/physical_memory_allocators/physical_memory_allocator.h"

#include <cstdlib>
#include <new>

namespace mimicos {

EmbedRadixPageTable::EmbedRadixPageTable(PhysicalMemoryAllocator* alloc, int levels,
                                         int entries_per_frame)
    : m_alloc(alloc), m_levels(levels), m_entries(entries_per_frame)
{
    m_root = new_frame();
}

EmbedRadixPageTable::~EmbedRadixPageTable()
{
    free_frame(m_root, 0);
    m_root = nullptr;
}

EmbedRadixPageTable::Frame* EmbedRadixPageTable::new_frame()
{
    Frame* f  = new Frame;
    f->entries = new Entry[m_entries];
    /* Interior tables are backed by real frames drawn from the MimicOS kernel
       reserve, exactly as Sniper's PageTableRadix does via
       handle_page_table_allocations().  The returned value is a PPN. */
    f->ppn = m_alloc ? m_alloc->handle_page_table_allocations(kFrameBytes) : 0;
    m_frames_allocated++;
    return f;
}

void EmbedRadixPageTable::free_frame(Frame* f, int depth)
{
    if (f == nullptr)
        return;
    if (depth < m_levels - 1) {
        for (int i = 0; i < m_entries; i++) {
            /* A promoted huge leaf retains its subtree, so recurse on `next`
               regardless of is_pte or the subtree leaks. */
            if (f->entries[i].next != nullptr)
                free_frame(f->entries[i].next, depth + 1);
        }
    }
    delete[] f->entries;
    delete f;
}

bool EmbedRadixPageTable::lookup(uint64_t va, uint64_t* ppn, uint32_t* page_size_bits) const
{
    Frame* cur = m_root;
    for (int d = 0; d < m_levels; d++) {
        uint32_t idx = index_at(va, static_cast<uint32_t>(d));
        Entry& e = cur->entries[idx];
        if (e.is_pte) {
            if (!e.valid)
                return false;
            if (ppn)            *ppn = e.ppn;
            if (page_size_bits) *page_size_bits = e.page_size_bits;
            return true;
        }
        if (e.next == nullptr)
            return false;
        cur = e.next;
    }
    return false;
}

uint32_t EmbedRadixPageTable::leaf_depth(uint64_t va) const
{
    Frame* cur = m_root;
    for (int d = 0; d < m_levels; d++) {
        uint32_t idx = index_at(va, static_cast<uint32_t>(d));
        Entry& e = cur->entries[idx];
        if (e.is_pte && e.valid)
            return static_cast<uint32_t>(d);
        if (e.is_pte || e.next == nullptr)
            break;
        cur = e.next;
    }
    /* Unmapped: assume a full-depth walk. */
    return static_cast<uint32_t>(m_levels - 1);
}

int EmbedRadixPageTable::install(uint64_t va, uint64_t ppn, uint32_t page_size_bits,
                                 bool* promoted)
{
    if (promoted)
        *promoted = false;

    const uint32_t leaf_d = leaf_depth_for_size(page_size_bits);
    if (leaf_d >= static_cast<uint32_t>(m_levels))
        return -1;   /* page size too large for this tree depth */

    Frame* cur = m_root;
    int allocated = 0;

    for (uint32_t d = 0; d < leaf_d; d++) {
        uint32_t idx = index_at(va, d);
        Entry& e = cur->entries[idx];
        if (e.is_pte) {
            if (e.valid) {
                /* A *larger* live page already covers this address.  Installing
                   a smaller one underneath would leave two live translations
                   for the same VA; the caller must unmap (demote) first. */
                return -1;
            }
            /* Stale leaf left behind by unmap(): this is the demotion path.
               Clear the leaf marker and descend into the retained subtree. */
            e.is_pte = false;
        }
        if (e.next == nullptr) {
            Frame* child = new_frame();
            if (child == nullptr)
                return -1;
            e.next = child;
            allocated++;
        }
        cur = e.next;
    }

    Entry& leaf = cur->entries[index_at(va, leaf_d)];
    /* Promotion: this entry currently points at a finer-grained subtree.  Keep
       the subtree (demotion can reuse it) but mark the entry as a huge leaf so
       lookups and walks terminate here. */
    if (!leaf.is_pte && leaf.next != nullptr && promoted)
        *promoted = true;

    leaf.is_pte         = true;
    leaf.valid          = true;
    leaf.ppn            = ppn;
    leaf.page_size_bits = page_size_bits;
    return allocated;
}

void EmbedRadixPageTable::unmap(uint64_t va)
{
    Frame* cur = m_root;
    for (int d = 0; d < m_levels; d++) {
        uint32_t idx = index_at(va, static_cast<uint32_t>(d));
        Entry& e = cur->entries[idx];
        if (e.is_pte) {
            e.valid = false;
            e.ppn   = 0;
            return;
        }
        if (e.next == nullptr)
            return;
        cur = e.next;
    }
}

bool EmbedRadixPageTable::entry_address(uint64_t va, uint32_t depth, bool allocate,
                                        uint64_t* pte_paddr, bool* is_leaf,
                                        uint32_t* frames_allocated)
{
    if (depth >= static_cast<uint32_t>(m_levels))
        return false;

    Frame* cur = m_root;
    for (uint32_t d = 0; d < depth; d++) {
        uint32_t idx = index_at(va, d);
        Entry& e = cur->entries[idx];
        if (e.is_pte) {
            /* A huge page terminates the walk above the requested depth, so
               the requested entry does not exist. */
            return false;
        }
        if (e.next == nullptr) {
            if (!allocate)
                return false;
            Frame* child = new_frame();
            if (child == nullptr)
                return false;
            e.next = child;
            if (frames_allocated)
                (*frames_allocated)++;
        }
        cur = e.next;
    }

    uint32_t idx = index_at(va, depth);
    if (pte_paddr)
        *pte_paddr = cur->ppn * kFrameBytes + static_cast<uint64_t>(idx) * kEntryBytes;
    if (is_leaf)
        *is_leaf = cur->entries[idx].is_pte && cur->entries[idx].valid;
    return true;
}

} // namespace mimicos
