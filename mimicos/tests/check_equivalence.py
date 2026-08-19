#!/usr/bin/env python3
"""Cross-simulator equivalence check for MimicOS.

Two things are checked, and they are different claims.

--config/--trace replays a simulator's own fault sequence against the
library alone and compares row for row.  That proves the adapter is a
pass-through: given one sequence of faults, MimicOS decides the same
things whether or not a simulator is driving it.

--compare --unordered compares two simulators to each other.  They do
not fault in the same order and do not have to: MimicOS hands out frames
as faults arrive, so a different order means different frames for the
same pages, exactly as a real OS would.  What must hold is that both map
the same pages, at the same sizes, one frame each.

A host simulator running with MimicOS attached and MIMICOS_MAPPING_TRACE set
emits one CSV row per mapping decision.  This tool replays that trace's
(asid, vpn) sequence against libmimicos on its own and asserts the two agree,
row for row.

Why this shape: ChampSim, gem5 and Sniper cannot consume the same trace format,
so they can never be given a byte-identical input stream.  What *can* be
established is that each one drives MimicOS identically -- that the adapter is
a pass-through and does not perturb allocator state.  If every simulator agrees
with the same reference, they agree with each other on any virtual-address
sequence they share.

A mismatch means the adapter changed MimicOS's decisions: a stray translate(),
a request built with the wrong asid, or allocator state mutated out of order.

Usage:
    check_equivalence.py --config <mimicos.ini> --trace <simulator_trace.csv>
                         [--replay-binary build/mimicos_replay] [--keep]

    # compare two simulators' traces to each other directly, when their
    # virtual-address streams really are the same:
    check_equivalence.py --compare a.csv b.csv
"""

import argparse
import csv
import os
import subprocess
import sys
import tempfile

FIELDS = ["seq", "asid", "vpn", "base_ppn", "page_size_bits", "pt_frames",
          "fault_latency_ns"]

# What MimicOS decided. These must match exactly: they are the allocator's
# output, and nothing about the host simulator should influence them.
PLACEMENT_FIELDS = ["asid", "vpn", "base_ppn", "page_size_bits"]

# What the fault was charged. A host that issues concurrent page-table walks
# can materialise an interior table during one walk and complete a different
# walk first, so *which* fault is billed for the frame varies. The totals are
# still conserved, so these are compared in aggregate rather than row by row.
ATTRIBUTION_FIELDS = ["pt_frames", "fault_latency_ns"]


def read_trace(path):
    """Read a mapping trace, returning (metadata, rows)."""
    metadata = {}
    lines = []
    with open(path, newline="") as handle:
        for line in handle:
            if line.startswith("#"):
                for token in line.lstrip("#").split():
                    if "=" in token:
                        key, _, value = token.partition("=")
                        metadata[key] = value
                continue
            lines.append(line)

    reader = csv.DictReader(lines)
    missing = [f for f in FIELDS if f not in (reader.fieldnames or [])]
    if missing:
        sys.exit(f"{path}: not a MimicOS mapping trace "
                 f"(missing columns: {', '.join(missing)})")
    return metadata, list(reader)


def check_metadata(name_a, meta_a, name_b, meta_b):
    """A geometry or allocator mismatch invalidates the comparison outright."""
    if not meta_a or not meta_b:
        print("  note: one trace has no metadata header; skipping config check")
        return 0

    mismatched = [k for k in sorted(set(meta_a) | set(meta_b))
                  if meta_a.get(k) != meta_b.get(k)]
    if not mismatched:
        return 0

    print(f"  CONFIG MISMATCH between {name_a} and {name_b}:")
    for key in mismatched:
        print(f"    {key}: {meta_a.get(key, '<absent>')} != "
              f"{meta_b.get(key, '<absent>')}")
    print("  The two runs used different MimicOS configurations, so any "
          "difference below is expected. Fix the config before reading further.")
    return len(mismatched)


def diff_unordered(name_a, rows_a, name_b, rows_b, limit=10):
    """Compare what does not depend on the order faults arrived in.

    MimicOS hands out frames as faults arrive, so two simulators that
    fault in different orders assign different frames to the same pages.
    That is not a disagreement; a real OS does the same.  What has to
    hold either way:

      - the same pages end up mapped
      - each page gets the same size in both
      - no two pages share a frame, in either
      - the same number of frames is consumed
    """
    fatal = 0

    pages_a = {r["vpn"]: r for r in rows_a}
    pages_b = {r["vpn"]: r for r in rows_b}

    # A page must not be mapped twice with different answers within a run.
    for name, rows in ((name_a, rows_a), (name_b, rows_b)):
        seen = {}
        unstable = 0
        for row in rows:
            prior = seen.get(row["vpn"])
            if prior is not None and prior != row["base_ppn"]:
                unstable += 1
            seen[row["vpn"]] = row["base_ppn"]
        if unstable:
            print(f"  UNSTABLE {name}: {unstable} pages remapped to a "
                  f"different frame within one run")
            fatal += 1

    only_a = set(pages_a) - set(pages_b)
    only_b = set(pages_b) - set(pages_a)
    if only_a or only_b:
        print(f"  PAGES: {len(only_a)} only in {name_a}, "
              f"{len(only_b)} only in {name_b}")
        for vpn in list(only_a)[:limit]:
            print(f"    only in {name_a}: vpn {vpn}")
        for vpn in list(only_b)[:limit]:
            print(f"    only in {name_b}: vpn {vpn}")
        fatal += 1
    else:
        print(f"  pages: both mapped the same {len(pages_a)} pages")

    size_diffs = [v for v in pages_a
                  if v in pages_b
                  and pages_a[v]["page_size_bits"] != pages_b[v]["page_size_bits"]]
    if size_diffs:
        print(f"  PAGE SIZE: {len(size_diffs)} pages differ")
        for vpn in size_diffs[:limit]:
            print(f"    vpn {vpn}: {pages_a[vpn]['page_size_bits']} vs "
                  f"{pages_b[vpn]['page_size_bits']}")
        fatal += 1
    else:
        sizes = {}
        for row in rows_a:
            sizes[row["page_size_bits"]] = sizes.get(row["page_size_bits"], 0) + 1
        shape = ", ".join(f"{n} at {b} bits" for b, n in sorted(sizes.items()))
        print(f"  page sizes: identical for every page ({shape})")

    # Two unrelated pages must not land on the same physical memory:
    # that would shrink the working set and flatter the cache numbers.
    #
    # Promotion is not that.  When a region is upgraded to 2M, the huge
    # mapping starts at the frame the region's first 4K page already
    # had, so the two overlap on purpose.  Only flag an overlap when the
    # virtual ranges are disjoint as well.
    for name, rows in ((name_a, rows_a), (name_b, rows_b)):
        spans = []
        for row in rows:
            pages = 1 << (int(row["page_size_bits"]) - 12)
            vpn = int(row["vpn"]) & ~(pages - 1)
            spans.append((int(row["base_ppn"]), pages, vpn))

        by_frame = {}
        aliased = []
        for ppn, pages, vpn in spans:
            for frame in range(ppn, ppn + pages):
                prior = by_frame.get(frame)
                if prior is not None:
                    prior_vpn, prior_pages = prior
                    # Same virtual region at a different granularity is
                    # a promotion, not an alias.
                    covers = (vpn <= prior_vpn < vpn + pages
                              or prior_vpn <= vpn < prior_vpn + prior_pages)
                    if not covers:
                        aliased.append((frame, prior_vpn, vpn))
                by_frame[frame] = (vpn, pages)

        if aliased:
            print(f"  ALIASED {name}: {len(aliased)} frames shared by "
                  f"unrelated pages")
            for frame, one, two in aliased[:limit]:
                print(f"    frame {frame}: vpn {one} and vpn {two}")
            fatal += 1

    def frames_used(rows):
        total = set()
        for row in rows:
            pages = 1 << (int(row["page_size_bits"]) - 12)
            base = int(row["base_ppn"])
            total.update(range(base, base + pages))
        return len(total)

    frames_a = frames_used(rows_a)
    frames_b = frames_used(rows_b)
    if frames_a != frames_b:
        print(f"  FRAMES: {name_a} used {frames_a}, {name_b} used {frames_b}")
        fatal += 1
    else:
        print(f"  frames: both consumed {frames_a}, none shared by unrelated pages")

    return fatal


def diff_rows(name_a, rows_a, name_b, rows_b, limit=10, ignore_asid=False):
    """Compare two mapping traces. Returns the count of *fatal* differences."""
    fatal = 0
    placement_fields = [f for f in PLACEMENT_FIELDS
                        if not (ignore_asid and f == "asid")]

    if len(rows_a) != len(rows_b):
        print(f"  row count differs: {name_a} has {len(rows_a)}, "
              f"{name_b} has {len(rows_b)}")
        return 1

    # --- placement: must be identical row for row ---
    shown = 0
    placement_diffs = 0
    for index, (row_a, row_b) in enumerate(zip(rows_a, rows_b)):
        delta = [f for f in placement_fields if row_a[f] != row_b[f]]
        if not delta:
            continue
        placement_diffs += 1
        if shown < limit:
            shown += 1
            detail = ", ".join(f"{f}: {row_a[f]} != {row_b[f]}" for f in delta)
            print(f"  PLACEMENT row {index} (vpn {row_a['vpn']}): {detail}")
    if placement_diffs > shown and shown == limit:
        print(f"  ... and {placement_diffs - shown} more differing rows")
    fatal += placement_diffs

    if placement_diffs == 0:
        print(f"  placement: all {len(rows_a)} mappings identical "
              f"(asid, vpn, frame, page size)")

    # --- attribution: totals must be conserved, per-row order may vary ---
    for field in ATTRIBUTION_FIELDS:
        total_a = sum(int(r[field]) for r in rows_a)
        total_b = sum(int(r[field]) for r in rows_b)
        reordered = sum(1 for x, y in zip(rows_a, rows_b) if x[field] != y[field])
        if total_a != total_b:
            print(f"  ATTRIBUTION {field}: totals differ, "
                  f"{name_a}={total_a} {name_b}={total_b}")
            fatal += 1
        elif reordered:
            print(f"  attribution {field}: totals agree ({total_a}), but "
                  f"{reordered} rows are billed to a different fault "
                  f"(expected when the host runs concurrent walks)")
        else:
            print(f"  attribution {field}: identical (total {total_a})")

    return fatal


def replay(config, trace_rows, replay_binary, keep):
    """Drive libmimicos with the trace's (asid, vpn) sequence."""
    if not os.path.exists(replay_binary):
        sys.exit(f"{replay_binary} not found. Build it with "
                 f"'make -C mimicos replay'.")

    vpn_fd, vpn_path = tempfile.mkstemp(suffix=".vpns", text=True)
    out_fd, out_path = tempfile.mkstemp(suffix=".csv")
    os.close(out_fd)

    try:
        with os.fdopen(vpn_fd, "w") as handle:
            for row in trace_rows:
                handle.write(f"{row['asid']} {row['vpn']}\n")

        result = subprocess.run(
            [replay_binary, config, vpn_path, out_path],
            capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr)
            sys.exit(f"{replay_binary} failed with exit code "
                     f"{result.returncode}")
        print(f"  reference: {result.stderr.strip()}")
        return read_trace(out_path)
    finally:
        if keep:
            print(f"  kept {vpn_path} and {out_path}")
        else:
            for path in (vpn_path, out_path):
                try:
                    os.unlink(path)
                except OSError:
                    pass


def main():
    parser = argparse.ArgumentParser(
        description="Check that a simulator drives MimicOS identically to the "
                    "library on its own")
    parser.add_argument("--config", help="MimicOS INI used by the simulator")
    parser.add_argument("--trace", help="mapping trace emitted by the simulator")
    parser.add_argument("--compare", nargs=2, metavar=("A", "B"),
                        help="compare two simulator traces directly")
    parser.add_argument("--replay-binary",
                        default=os.path.join(os.path.dirname(__file__), os.pardir,
                                             "build", "mimicos_replay"))
    parser.add_argument("--keep", action="store_true",
                        help="keep the intermediate files for inspection")
    parser.add_argument("--unordered", action="store_true",
                        help="compare properties that do not depend on the "
                             "order faults arrived in. Two simulators do not "
                             "have to fault in the same order; what has to "
                             "hold is that each one maps the same pages, at "
                             "the same sizes, one frame each.")
    parser.add_argument("--ignore-asid", action="store_true",
                        help="do not compare the asid column. Needed when "
                             "comparing two simulators directly: gem5 uses "
                             "the pid and ChampSim uses the cpu index, so the "
                             "numbers differ even though both name the same "
                             "single address space. Placement comes from the "
                             "allocator, which is shared, so it still has to "
                             "match.")
    args = parser.parse_args()

    if args.compare:
        path_a, path_b = args.compare
        print(f"Comparing {path_a} against {path_b}")
        meta_a, rows_a = read_trace(path_a)
        meta_b, rows_b = read_trace(path_b)
        differences = check_metadata(path_a, meta_a, path_b, meta_b)
        if args.unordered:
            differences += diff_unordered(path_a, rows_a, path_b, rows_b)
        else:
            differences += diff_rows(path_a, rows_a, path_b, rows_b,
                                     ignore_asid=args.ignore_asid)
    else:
        if not (args.config and args.trace):
            parser.error("--config and --trace are both required "
                         "(or use --compare)")
        meta, rows = read_trace(args.trace)
        print(f"Replaying {len(rows)} mappings from {args.trace} "
              f"against {args.config}")
        ref_meta, reference = replay(args.config, rows, args.replay_binary,
                                     args.keep)
        differences = check_metadata(args.trace, meta, "reference", ref_meta)
        differences += diff_rows(args.trace, rows, "reference", reference,
                                 ignore_asid=args.ignore_asid)

    if differences:
        print(f"MISMATCH: {differences} fatal difference(s)")
        return 1

    if args.compare and args.unordered:
        print("MATCH: both simulators mapped the same pages, at the same "
              "sizes, one frame each")
    else:
        print("MATCH: the simulator and the reference make identical "
              "allocation decisions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
