#!/usr/bin/env python3
"""Turn a list of virtual addresses into a ChampSim trace.

Both simulators drive the same MimicOS, but they cannot read the same
input: gem5 runs a binary, ChampSim reads a trace.  This closes that
gap.  Take the virtual pages gem5 actually touched, write a ChampSim
trace that touches the same pages in the same order, and the two
simulators are then driving MimicOS with one stream.  Their mapping
traces can be compared directly instead of each being checked against a
reference.

The generated instructions are as plain as the format allows: one load
per address, no branches, so nothing in the front end reorders the
access stream.

Input is one "asid vpn" pair per line, which is what the mapping trace
carries.  Output is a raw (optionally xz-compressed) ChampSim trace.

Usage:
    va_to_champsim_trace.py <vpn_list> <out.champsimtrace.xz> [--repeat N]
"""

import argparse
import lzma
import struct
import sys

# struct input_instr from ChampSim's inc/trace_instruction.h:
#   0  unsigned long long ip
#   8  unsigned char is_branch, branch_taken
#  10  unsigned char destination_registers[2]
#  12  unsigned char source_registers[4]
#  16  unsigned long long destination_memory[2]
#  32  unsigned long long source_memory[4]
# 64 bytes with no padding; the char fields land on 16, already aligned.
INSTR_FORMAT = "<Q2B2B4B2Q4Q"
REG_INSTRUCTION_POINTER = 26


# Optional dependency chain: each memory instruction writes this
# register and the next one reads it, so the core cannot reorder the
# accesses.  Off by default.  Fault order does not have to match between
# simulators; the comparison checks properties that do not depend on it.
CHAIN_REG = 10


def encode(ip, source_memory, chained=False):
    dest_regs = (CHAIN_REG, 0) if chained else (0, 0)
    src_regs = ((CHAIN_REG, REG_INSTRUCTION_POINTER, 0, 0) if chained
                else (REG_INSTRUCTION_POINTER, 0, 0, 0))
    dest_mem = (0, 0)
    src_mem = tuple(source_memory) + (0,) * (4 - len(source_memory))
    return struct.pack(INSTR_FORMAT, ip, 0, 0, *dest_regs, *src_regs,
                       *dest_mem, *src_mem)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("vpn_list")
    parser.add_argument("output")
    parser.add_argument("--repeat", type=int, default=1,
                        help="passes over the address list; more than one "
                             "exercises hits rather than faults")
    parser.add_argument("--ip-base", type=lambda x: int(x, 0), default=0x400000,
                        help="where the synthetic instructions live")
    parser.add_argument("--serialise", action="store_true",
                        help="chain the memory instructions through a register "
                             "so the core cannot reorder them. Only needed to "
                             "reproduce one exact fault order.")
    parser.add_argument("--spacing", type=int, default=8,
                        help="register-only instructions between each memory "
                             "access. Back-to-back misses to distinct pages "
                             "saturate the load queue and trip the deadlock "
                             "detector; spacing lets the machine drain.")
    args = parser.parse_args()

    if struct.calcsize(INSTR_FORMAT) != 64:
        sys.exit(f"input_instr layout is {struct.calcsize(INSTR_FORMAT)} bytes, expected 64")

    addresses = []
    with open(args.vpn_list) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                vpn = int(parts[1])
            except ValueError:
                # header row or other text; skip it
                continue
            addresses.append(vpn << 12)

    if not addresses:
        sys.exit(f"{args.vpn_list}: no addresses")

    opener = lzma.open if args.output.endswith(".xz") else open
    written = 0
    with opener(args.output, "wb") as out:
        for _ in range(args.repeat):
            for index, addr in enumerate(addresses):
                # Keep the instruction pointer inside one page so the
                # instruction side does not add faults of its own.
                ip = args.ip_base + ((index * 4) % 2048)
                out.write(encode(ip, [addr], chained=args.serialise))
                written += 1
                for filler in range(args.spacing):
                    out.write(encode(ip + 4 * (filler + 1), []))
                    written += 1

    print(f"wrote {written} instructions over {len(addresses)} addresses "
          f"to {args.output}")


if __name__ == "__main__":
    main()
