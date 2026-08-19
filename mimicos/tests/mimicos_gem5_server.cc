/*
 * @kanellok: MimicOS as a gem5 guest process.
 *
 * gem5's syscall emulation mode resolves a page fault by calling into
 * the simulator, so the cost of handling one can only ever be a number
 * from a config file.  Run this instead and the fault is handed out to
 * a real process: everything below executes as guest instructions, so
 * what a fault costs is what the handler actually does.
 *
 * The exchange is two m5ops.  m5_mimicos_wait() blocks until the
 * simulator has a fault and returns its address; m5_mimicos_reply()
 * says which frame backs it and how big the page is.  Between the two
 * is ordinary MimicOS: the same allocator and page tables the library
 * uses everywhere else.
 *
 * Run it alongside the application:
 *
 *   build/X86/gem5.opt configs/example/mimicos_se.py \
 *       --mimicos-config <ini> --mimicos-in-guest \
 *       --mimicos-server mimicos/build/mimicos_gem5_server <app>
 */
#include "mimicos_embed.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

/*
 * gem5 triggers an m5op on x86 with 0x0F 0x04 followed by the opcode
 * number; see util/m5/src/abi/x86/m5op.S.  Arguments and the return
 * value follow the ordinary SysV registers.
 */
#define MIMICOS_SHUTDOWN (~(uint64_t)0)
#define MIMICOS_NO_WORK  (~(uint64_t)1)

static inline uint64_t m5_mimicos_wait(void)
{
    uint64_t va;
    __asm__ __volatile__(".byte 0x0F, 0x04\n\t.word 0x71"
                         : "=a"(va)
                         :
                         : "memory");
    return va;
}

/* Which address space the fault being served belongs to. */
static inline uint64_t m5_mimicos_asid(void)
{
    uint64_t asid;
    __asm__ __volatile__(".byte 0x0F, 0x04\n\t.word 0x73"
                         : "=a"(asid)
                         :
                         : "memory");
    return asid;
}

static inline void m5_mimicos_reply(uint64_t ppn, uint64_t page_size_bits)
{
    __asm__ __volatile__(".byte 0x0F, 0x04\n\t.word 0x72"
                         :
                         : "D"(ppn), "S"(page_size_bits)
                         : "memory");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <config.ini> [max_faults]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const std::string ini = argv[1];
    const unsigned long limit =
        (argc > 2) ? std::strtoul(argv[2], nullptr, 0) : 0;

    std::string err;
    auto kernel = mimicos::Kernel::create_from_ini(ini, &err);
    if (kernel == nullptr) {
        std::fprintf(stderr, "mimicos server: %s\n", err.c_str());
        return EXIT_FAILURE;
    }

    std::printf("mimicos server: ready\n");
    std::fflush(stdout);

    unsigned long served = 0;
    for (;;) {
        /*
         * Parks this process until the simulator has a fault.  A zero
         * means it woke us with nothing to do, so just ask again.
         */
        /*
         * Address 0 is a real address, so it cannot mean "nothing to
         * do".  The simulator uses two values out of the address space
         * for that instead.
         */
        const uint64_t vaddr = m5_mimicos_wait();
        if (vaddr == MIMICOS_SHUTDOWN)
            break;
        if (vaddr == MIMICOS_NO_WORK)
            continue;

        /*
         * One page table per address space, so a fault has to say which
         * one it came from.  Threads of a process share it; separate
         * processes do not.
         */
        mimicos::FaultRequest req;
        req.vaddr = vaddr;
        req.asid = (uint32_t)m5_mimicos_asid();
        req.core_id = 0;
        req.access = mimicos::AccessType::WRITE;
        req.mapping = mimicos::MappingType::ANON;

        const auto t = kernel->translate(req);
        if (!t.ok) {
            std::fprintf(stderr, "mimicos server: out of memory at %#llx\n",
                         (unsigned long long)vaddr);
            return EXIT_FAILURE;
        }

        m5_mimicos_reply(t.ppn, t.page_size_bits);

        if (++served == limit)
            break;
    }

    const auto stats = kernel->stats();
    std::printf("mimicos server: %lu served, %llu faults, %llu huge, "
                "%llu aliased\n", served,
                (unsigned long long)stats.minor_faults,
                (unsigned long long)stats.huge_page_faults,
                (unsigned long long)stats.aliased_frames);
    return EXIT_SUCCESS;
}
