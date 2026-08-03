/**
 * @file address_space.h
 * @brief Per-process address-space observability for MimicOS (Stage C, Apr 16 2026).
 *
 * Provides a per-AddressSpace VMA tree and file-descriptor table.  Maintained
 * by hooking mmap/munmap/mprotect/open/openat/close syscalls as they flow
 * through the SIFT stream into Sniper's TraceThread.  Observability only:
 * no access checks, no PROT_NONE enforcement — the kernel reads the VMA
 * metadata for a faulting address and logs it alongside fault statistics.
 *
 * Data structures:
 *   - VmaTree: std::map<start, Vma> keyed by VMA start address.  VMAs are
 *     non-overlapping by construction.  Containment query is
 *     upper_bound(addr) → step back → check end > addr.  O(log n).
 *   - FdTable: std::unordered_map<int, FdEntry>.  open/openat inserts,
 *     close erases.
 *   - AddressSpace: pairs VmaTree + FdTable + a per-instance shared_mutex.
 *     Fault lookups take the shared lock; syscall updates take unique.
 */
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MimicOS_NS {

struct Vma {
    uint64_t    start  = 0;        // inclusive
    uint64_t    end    = 0;        // exclusive
    int         prot   = 0;        // PROT_READ|PROT_WRITE|PROT_EXEC
    int         flags  = 0;        // MAP_PRIVATE / MAP_ANONYMOUS / ...
    int         fd     = -1;       // -1 for anonymous
    uint64_t    offset = 0;        // file offset for file-backed
    std::string path;              // resolved via FdTable at creation time

    uint64_t size() const { return end - start; }
    bool     contains(uint64_t a) const { return a >= start && a < end; }
    bool     is_anon() const { return fd < 0; }
};

struct FdEntry {
    std::string path;
    int         flags = 0;
    uint64_t    offset = 0;
};

class AddressSpace {
public:
    AddressSpace() = default;
    AddressSpace(const AddressSpace&) = delete;
    AddressSpace& operator=(const AddressSpace&) = delete;

    /* ---------------- VMA mutations (writer) ---------------- */

    /* Register a new VMA.  If it overlaps existing ones, those are split
       or replaced to honour the mmap-style "replace any existing mapping"
       semantics.  Returns the canonical VMA that now covers [start,end). */
    void insert_vma(uint64_t start, uint64_t end,
                    int prot, int flags, int fd, uint64_t offset);

    /* Remove [start,end); splits VMAs at the edges if needed. */
    void remove_vma(uint64_t start, uint64_t end);

    /* Change protection of [start,end); splits VMAs at edges if needed. */
    void mprotect(uint64_t start, uint64_t end, int prot);

    /* ---------------- VMA queries (reader) ------------------ */

    /* Snapshot of the VMA containing `addr`, if any.  Returns an owned
       copy to keep lifetime simple across lock boundaries. */
    std::optional<Vma> vma_containing(uint64_t addr) const;

    /* Number of VMAs currently registered. */
    size_t vma_count() const;

    /* Snapshot all VMAs under the shared lock.  Useful for dumps. */
    std::vector<Vma> snapshot_vmas() const;

    /* ---------------- FD table ------------------------------- */

    void fd_open(int fd, const std::string &path, int flags);
    void fd_close(int fd);
    std::optional<FdEntry> fd_lookup(int fd) const;

private:
    mutable std::shared_mutex       m_lock;
    std::map<uint64_t, Vma>         m_vmas;  // keyed by start
    std::unordered_map<int, FdEntry> m_fds;
};

/* Global registry — lazily creates an AddressSpace per app_id.  Thread-safe. */
class AddressSpaceRegistry {
public:
    static AddressSpaceRegistry& instance();
    AddressSpace* get(int app_id);
    /* Log a human-readable summary of VMA/FD state to `path`.  Called from
       MimicOS shutdown. */
    void dump(const std::string &path) const;
private:
    mutable std::mutex                                     m_lock;
    std::map<int, std::unique_ptr<AddressSpace>>           m_spaces;
};

} // namespace MimicOS_NS
