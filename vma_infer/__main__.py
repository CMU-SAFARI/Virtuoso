"""
vma_infer.__main__ – CLI entry point.

Usage::

    python -m vma_infer --trace <path> [options]

Run ``python -m vma_infer --help`` for the full option list.
"""

from __future__ import annotations

import argparse
import sys
import time

from .trace_reader import iter_trace
from .infer import infer_regions
from .report import print_table, write_json


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="vma_infer",
        description="Infer candidate VMAs from a ChampSim trace file.",
    )
    p.add_argument(
        "--trace", required=True,
        help="Path to the trace file (may be .gz compressed).",
    )
    p.add_argument(
        "--format", dest="fmt", default="auto",
        choices=["auto", "champsim", "champsim_txt"],
        help="Trace format (default: auto-detect).",
    )
    p.add_argument(
        "--page-size", type=int, default=4096,
        help="Virtual page size in bytes (default: 4096).",
    )
    p.add_argument(
        "--gap-pages", type=int, default=4,
        help="Max gap (pages) allowed when merging runs (default: 4).",
    )
    p.add_argument(
        "--min-pages", type=int, default=8,
        help="Discard regions with fewer touched pages (default: 8).",
    )
    p.add_argument(
        "--time-split-ms", type=int, default=50,
        help=(
            "Split threshold for first-touch discontinuity in "
            "record-index units (default: 50).  When real timestamps "
            "are absent each record counts as 1 unit."
        ),
    )
    p.add_argument(
        "--wf-window", type=int, default=64,
        help="Sliding window size (pages) for write-frac split (default: 64).",
    )
    p.add_argument(
        "--wf-split-delta", type=float, default=0.2,
        help="Min write-frac change to trigger a split (default: 0.2).",
    )
    p.add_argument(
        "--out", default=None,
        help="Write JSON output to this path.",
    )
    p.add_argument(
        "--limit", type=int, default=0,
        help="Only process the first N records (0 = unlimited).",
    )
    return p


def main(argv=None):
    args = _build_parser().parse_args(argv)

    print(f"[vma_infer] Reading trace: {args.trace}", file=sys.stderr)
    t0 = time.time()

    accesses = iter_trace(args.trace, fmt=args.fmt, limit=args.limit)
    regions = infer_regions(
        accesses,
        page_size=args.page_size,
        gap_pages=args.gap_pages,
        min_pages=args.min_pages,
        time_split_units=args.time_split_ms,
        wf_window=args.wf_window,
        wf_split_delta=args.wf_split_delta,
    )

    elapsed = time.time() - t0
    print(
        f"[vma_infer] Done in {elapsed:.2f}s – found {len(regions)} region(s).",
        file=sys.stderr,
    )

    print_table(regions)

    if args.out:
        write_json(
            regions,
            args.out,
            page_size=args.page_size,
            extra_meta={
                "trace": args.trace,
                "format": args.fmt,
                "gap_pages": args.gap_pages,
                "min_pages": args.min_pages,
                "time_split_ms": args.time_split_ms,
            },
        )
        print(f"[vma_infer] JSON written to {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
