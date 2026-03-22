#ifndef SPOT_EXCEPTION_HANDLER_H
#define SPOT_EXCEPTION_HANDLER_H

#include "misc/exception_handler_base.h"
#include <fstream>

/**
 * @brief Spot (on-demand) page fault exception handler
 * 
 * This handler allocates pages on-demand as page faults occur,
 * similar to the default handler but with support for both host and guest modes.
 */
class SpotExceptionHandler : public ExceptionHandlerBase {
private:
    std::ofstream log_file;
    bool is_guest;  // Whether this handler is for guest (VM) mode

public:
    SpotExceptionHandler(Core* core, bool is_guest = false);
    ~SpotExceptionHandler();

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
};

#endif // SPOT_EXCEPTION_HANDLER_H
