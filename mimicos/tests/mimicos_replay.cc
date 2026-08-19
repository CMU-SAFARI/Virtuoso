/**
 * @file mimicos_replay.cc
 * @brief Reference driver for cross-simulator equivalence testing.
 *
 * Reads a list of (asid, vpn) pairs and feeds them to libmimicos in order,
 * emitting the same mapping-trace CSV that Kernel::enable_mapping_trace()
 * writes from inside a host simulator.
 *
 * The equivalence argument runs like this: a host simulator with MimicOS
 * attached emits a mapping trace; that trace's (asid, vpn) column is replayed
 * here against the same INI; the two CSVs must be identical.  If ChampSim and
 * gem5 both agree with this reference, they agree with each other on any
 * virtual-address stream they share -- without needing the two simulators to
 * consume the same trace format, which they cannot.
 *
 * A difference means the adapter perturbed MimicOS's decisions: an extra
 * translate() somewhere, a request built with the wrong asid, or allocator
 * state mutated out of order.
 *
 * Usage:
 *   mimicos_replay <config.ini> <vpn_list> <output.csv>
 *
 * vpn_list is one "asid vpn" pair per line (decimal), which is what
 * tests/mapping_trace_tool.py extracts from a simulator's mapping trace.
 */
#include "mimicos_embed.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <config.ini> <vpn_list> <output.csv>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const std::string ini_path = argv[1];
    const std::string vpn_path = argv[2];
    const std::string out_path = argv[3];

    std::string err;
    auto kernel = mimicos::Kernel::create_from_ini(ini_path, &err);
    if (kernel == nullptr) {
        std::fprintf(stderr, "mimicos_replay: %s\n", err.c_str());
        return EXIT_FAILURE;
    }

    if (!kernel->enable_mapping_trace(out_path, &err)) {
        std::fprintf(stderr, "mimicos_replay: %s\n", err.c_str());
        return EXIT_FAILURE;
    }

    std::ifstream in(vpn_path);
    if (!in) {
        std::fprintf(stderr, "mimicos_replay: cannot read '%s'\n", vpn_path.c_str());
        return EXIT_FAILURE;
    }

    std::string line;
    uint64_t replayed = 0;
    uint64_t failed = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream iss(line);
        uint64_t asid = 0, vpn = 0;
        if (!(iss >> asid >> vpn))
            continue;

        mimicos::FaultRequest req;
        req.vaddr = vpn << 12;
        req.asid = static_cast<uint32_t>(asid);
        req.core_id = static_cast<uint32_t>(asid);
        req.access = mimicos::AccessType::WRITE;
        req.mapping = mimicos::MappingType::ANON;

        const auto t = kernel->translate(req);
        if (!t.ok)
            failed++;
        replayed++;
    }

    kernel->flush_mapping_trace();

    const auto stats = kernel->stats();
    std::fprintf(stderr,
                 "mimicos_replay: %llu requests, %llu minor faults (%llu huge), "
                 "%llu data frames, %llu page-table frames, %llu failures\n",
                 (unsigned long long)replayed,
                 (unsigned long long)stats.minor_faults,
                 (unsigned long long)stats.huge_page_faults,
                 (unsigned long long)stats.data_frames_allocated,
                 (unsigned long long)stats.pt_frames_allocated,
                 (unsigned long long)failed);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
