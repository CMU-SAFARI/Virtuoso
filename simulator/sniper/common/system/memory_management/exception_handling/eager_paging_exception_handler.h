#ifndef EAGER_PAGING_EXCEPTION_HANDLER_H
#define EAGER_PAGING_EXCEPTION_HANDLER_H

#include "misc/exception_handler_base.h"
#include <fstream>

/**
 * @brief Exception handler that performs eager paging
 * 
 * This handler allocates entire VMAs eagerly when a page fault occurs,
 * rather than allocating pages on-demand.
 */
class EagerPagingExceptionHandler : public ExceptionHandlerBase {
private:
    std::ofstream log_file;

public:
    EagerPagingExceptionHandler(Core* core);
    ~EagerPagingExceptionHandler();

    void handle_exception(int exception_type_code, int argc, uint64_t *argv) override;
    void handle_page_fault(FaultCtx& ctx) override;
    PhysicalMemoryAllocator* getAllocator() override;

    /**
     * @brief Initialize a FaultCtx for this handler
     */
    ExceptionHandlerBase::FaultCtx initFaultCtx(ParametricDramDirectoryMSI::PageTable* page_table, 
                                                 UInt64 address, UInt64 core_id, int max_level) {
        ExceptionHandlerBase::FaultCtx fault_ctx{};
        fault_ctx.vpn = address >> 12;
        fault_ctx.page_table = page_table;
        fault_ctx.alloc_in.metadata_frames = max_level;
        return fault_ctx;
    }

private:
    // Helper functions
    void allocate_page_table_frames(FaultCtx& ctx,
                                    UInt64 address, UInt64 core_id, UInt64 ppn, 
                                    int page_size, int num_requested_frames);
    int update_page_table_frames(UInt64 address, UInt64 core_id, UInt64 ppn, 
                                  int page_size, std::vector<UInt64>& frames);
    void allocate_vma_eagerly(FaultCtx& ctx, UInt64 address, int app_id, int frames);
};

#endif // EAGER_PAGING_EXCEPTION_HANDLER_H
