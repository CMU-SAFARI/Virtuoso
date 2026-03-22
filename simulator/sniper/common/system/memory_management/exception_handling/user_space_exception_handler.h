#include "misc/exception_handler_base.h"

class VirtuosExceptionHandler : public ExceptionHandlerBase {
public:
    VirtuosExceptionHandler(Core* core);
    ~VirtuosExceptionHandler();

    void handle_exception(int exception_type_code, int argc, uint64_t *argv) override;
    void handle_page_fault(FaultCtx& ctx) override;
    PhysicalMemoryAllocator* getAllocator() override; 

private:
    int update_page_table_frames(ParametricDramDirectoryMSI::PageTable* page_table, UInt64 address, UInt64 core_id, UInt64 ppn,
                                 int page_size, const std::vector<UInt64>& frames_already_allocated_by_virtuos);

};