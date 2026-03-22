"""
vma_infer.report – Human-readable and JSON output for inferred regions.
"""

from __future__ import annotations

import json
import sys
from typing import IO, List, Optional

from .infer import Region


# ---------------------------------------------------------------------------
# Human-readable table
# ---------------------------------------------------------------------------

_HDR_FMT = (
    "{:<20s} {:<20s} {:>10s} {:>8s} {:>7s} {:>10s} {:>10s} {:>6s} "
    "{:>10s} {:>10s}  {}"
)
_ROW_FMT = (
    "{:<20s} {:<20s} {:>10d} {:>8d} {:>7.3f} {:>10d} {:>10d} {:>6.3f} "
    "{:>10d} {:>10d}  {}"
)


def print_table(regions: List[Region], file: IO[str] = sys.stdout) -> None:
    """Print one-line-per-region summary sorted by ``start_addr``."""
    header = _HDR_FMT.format(
        "start_addr", "end_addr", "span_pgs", "touched", "density",
        "reads", "writes", "wfrac", "first_ts", "last_ts", "label",
    )
    sep = "-" * len(header)
    print(sep, file=file)
    print(header, file=file)
    print(sep, file=file)
    for r in regions:
        print(
            _ROW_FMT.format(
                f"0x{r.start_addr:016x}",
                f"0x{r.end_addr:016x}",
                r.num_pages_span,
                r.num_pages_touched,
                r.touch_density,
                r.reads,
                r.writes,
                r.write_frac,
                r.first_touch_ts,
                r.last_touch_ts,
                r.label,
            ),
            file=file,
        )
    print(sep, file=file)
    print(f"Total regions: {len(regions)}", file=file)


# ---------------------------------------------------------------------------
# JSON export
# ---------------------------------------------------------------------------

def _region_to_dict(r: Region) -> dict:
    return {
        "start_addr": f"0x{r.start_addr:016x}",
        "end_addr": f"0x{r.end_addr:016x}",
        "start_addr_int": r.start_addr,
        "end_addr_int": r.end_addr,
        "num_pages_span": r.num_pages_span,
        "num_pages_touched": r.num_pages_touched,
        "touch_density": round(r.touch_density, 6),
        "reads": r.reads,
        "writes": r.writes,
        "write_frac": round(r.write_frac, 6),
        "first_touch_ts": r.first_touch_ts,
        "last_touch_ts": r.last_touch_ts,
        "label": r.label,
    }


def write_json(
    regions: List[Region],
    path: str,
    *,
    page_size: int = 4096,
    extra_meta: Optional[dict] = None,
) -> None:
    """Write inferred regions to a JSON file at *path*."""
    payload = {
        "page_size": page_size,
        "num_regions": len(regions),
        "regions": [_region_to_dict(r) for r in regions],
    }
    if extra_meta:
        payload["meta"] = extra_meta
    with open(path, "w") as fh:
        json.dump(payload, fh, indent=2)
        fh.write("\n")
