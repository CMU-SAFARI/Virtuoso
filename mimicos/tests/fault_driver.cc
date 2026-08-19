/*
 * @kanellok: run this under a tracer to capture what a fault really
 * executes.  ChampSim replays the result.
 *
 * Driver used to record what MimicOS actually executes to service a
 * minor fault.
 *
 * Run this under a tracer (ChampSim's PIN tool) and the resulting trace
 * contains the fault handler's real instruction stream.  Every fault is
 * bracketed by fault_marker(), which is a handful of nops at a known
 * address, so the trace can be cut into one fault per segment
 * afterwards.  Print the marker address so the splitter knows what to
 * look for.
 *
 * Nothing else happens in the loop, so whatever lies between two
 * markers is the fault and only the fault.
 */
#include "mimicos_embed.h"

#include <cstdio>
#include <cstdlib>
#include <string>

/*
 * noinline and noclone keep this as one recognisable block.  The nops
 * give it a shape no compiler-generated code will accidentally match.
 */
__attribute__((noinline, noclone)) void fault_marker(void)
{
    asm volatile("nop; nop; nop; nop; nop; nop; nop; nop" ::: "memory");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <config.ini> [faults] [stride_pages]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const std::string ini = argv[1];
    const unsigned long faults = (argc > 2) ? std::strtoul(argv[2], nullptr, 0) : 64;
    /*
     * A stride of one page keeps every fault inside the same region,
     * which is the common case.  Larger strides walk into fresh regions
     * and make the allocator do more work.
     */
    const unsigned long stride = (argc > 3) ? std::strtoul(argv[3], nullptr, 0) : 1;

    std::string err;
    auto kernel = mimicos::Kernel::create_from_ini(ini, &err);
    if (kernel == nullptr) {
        std::fprintf(stderr, "fault_driver: %s\n", err.c_str());
        return EXIT_FAILURE;
    }

    /* The splitter needs this to find the marker in the trace. */
    std::printf("marker_addr 0x%llx\n",
                (unsigned long long)(uintptr_t)&fault_marker);
    std::printf("faults %lu stride %lu\n", faults, stride);
    std::fflush(stdout);

    unsigned long served = 0;
    uint64_t va = 0x100000000ull;

    for (unsigned long i = 0; i < faults; i++) {
        fault_marker();

        mimicos::FaultRequest req;
        req.vaddr = va;
        req.asid = 0;
        req.core_id = 0;
        req.access = mimicos::AccessType::WRITE;
        req.mapping = mimicos::MappingType::ANON;

        const auto t = kernel->translate(req);
        if (t.ok && t.faulted)
            served++;

        va += stride * 4096ull;
    }

    fault_marker();

    const auto stats = kernel->stats();
    std::printf("served %lu faults, %llu minor faults, %llu huge\n", served,
                (unsigned long long)stats.minor_faults,
                (unsigned long long)stats.huge_page_faults);
    return EXIT_SUCCESS;
}
