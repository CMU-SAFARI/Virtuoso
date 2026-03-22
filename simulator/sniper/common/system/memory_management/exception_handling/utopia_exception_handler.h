#ifndef UTOPIA_EXCEPTION_HANDLER_H
#define UTOPIA_EXCEPTION_HANDLER_H

#include "misc/exception_handler_base.h"
#include "memory_management/physical_memory_allocators/utopia.h"
#include "sim_log.h"
#include <fstream>

/**
 * @brief Utopia-aware page fault exception handler
 * 
 * This handler is designed specifically for the Utopia allocator's 4-level
 * allocation hierarchy. The key insight is that RestSeg allocations use
 * range-based translation (no page tables needed), while FlexSeg allocations
 * require conventional page table entries.
 * 
 * ## Allocation Hierarchy and Page Table Handling:
 * 
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │                    UTOPIA EXCEPTION HANDLER                         │
 *   │              Page Table Update Decision Logic                       │
 *   └─────────────────────────────────────────────────────────────────────┘
 *   
 *   ┌──────────────────┐                ┌──────────────────┐
 *   │    RestSeg       │                │    FlexSeg       │
 *   │  (Range-based)   │                │  (Page Tables)   │
 *   └────────┬─────────┘                └────────┬─────────┘
 *            │                                   │
 *            ▼                                   ▼
 *   ┌─────────────────┐                ┌─────────────────┐
 *   │   NO PAGE TABLE │                │  UPDATE PAGE    │
 *   │   UPDATE NEEDED │                │  TABLE ENTRIES  │
 *   │   ─────────────│                 │  ─────────────  │
 *   │ • L1: RestSeg-2MB│                │ • L2: FlexSeg-THP│
 *   │ • L3: RestSeg-4KB│                │ • L4: FlexSeg-4KB│
 *   └─────────────────┘                └─────────────────┘
 *   
 * ## Why This Matters:
 * 
 * - RestSeg uses Utopia's range-based translation hardware
 *   → Virtual-to-physical is: PA = VA + offset (per RestSeg region)
 *   → No PTEs needed, no page walks, constant-time translation
 *   
 * - FlexSeg uses conventional page tables
 *   → Requires PTEs for each mapping
 *   → May need to allocate intermediate page table frames
 * 
 * ## Performance Implications:
 * 
 * By skipping page table updates for RestSeg allocations:
 *   1. Reduced memory overhead (no PT frames allocated)
 *   2. Faster page fault handling
 *   3. Better TLB efficiency (RestSeg entries cover large regions)
 */
class UtopiaExceptionHandler : public ExceptionHandlerBase {
private:
    std::ofstream log_file;
    SimLog sim_log;
    bool is_guest;  // Whether this handler is for guest (VM) mode

public:
    UtopiaExceptionHandler(Core* core, bool is_guest = false);
    ~UtopiaExceptionHandler();

    void handle_exception(int exception_type_code, int argc, uint64_t *argv) override;
    void handle_page_fault(FaultCtx& ctx) override;
    PhysicalMemoryAllocator* getAllocator() override;

    /**
     * @brief Initialize a FaultCtx for this handler
     * 
     * For Utopia, we set metadata_frames to the max level but the actual
     * allocation may skip page table frames entirely if RestSeg is used.
     */
    ExceptionHandlerBase::FaultCtx initFaultCtx(ParametricDramDirectoryMSI::PageTable* page_table, 
                                                 UInt64 address, UInt64 core_id, int max_level) {
        ExceptionHandlerBase::FaultCtx fault_ctx{};
        fault_ctx.vpn = address >> 12;
        fault_ctx.page_table = page_table;
        fault_ctx.alloc_in.metadata_frames = max_level;
        return fault_ctx;
    }

    /**
     * @brief Set whether this handler operates in guest mode
     */
    void setGuestMode(bool guest) { is_guest = guest; }

private:
    // Helper functions
    void allocate_page_table_frames(FaultCtx& ctx,
                                    UInt64 address, UInt64 core_id, UInt64 ppn, 
                                    int page_size, int num_requested_frames);
    int update_page_table_frames(UInt64 address, UInt64 core_id, UInt64 ppn, 
                                  int page_size, std::vector<UInt64>& frames);
    
    /**
     * @brief Check if the allocator is a Utopia allocator and if last allocation was in RestSeg
     * @return true if RestSeg was used (no page table update needed)
     */
    bool checkLastAllocWasRestSeg();
    
    /**
     * @brief Get the Utopia allocator (casts from base PhysicalMemoryAllocator)
     * @return Pointer to UtopiaAllocator or nullptr if not Utopia type
     */
    template<typename Policy>
    UtopiaAllocator<Policy>* getUtopiaAllocator();
};

#endif // UTOPIA_EXCEPTION_HANDLER_H
