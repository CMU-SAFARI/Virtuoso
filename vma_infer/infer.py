"""
vma_infer.infer – Cluster virtual pages into candidate VMA regions.

Algorithm (two-pass, memory-efficient)
======================================

**Pass 1** – stream all ``MemAccess`` records and maintain per-VPN aggregates
(read/write counts, first/last record index).  Only dicts keyed by VPN are
kept; individual records are *not* stored.

**Pass 2** –

1. Sort the unique VPNs.
2. Merge adjacent VPNs into *runs* allowing small gaps (``gap_pages``).
3. Optionally **split** each run using:
   a. *First-touch time discontinuity* – if the difference in ``first_idx``
      between consecutive pages exceeds ``time_split_units``, split there.
   b. *Write-fraction change* – compute ``write_frac`` in a sliding window
      (``wf_window`` pages) and split when the delta exceeds
      ``wf_split_delta``.
4. Compute per-region features and apply classification heuristics.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Dict, Generator, List, Optional, Sequence, Tuple

from .trace_reader import MemAccess


# ---------------------------------------------------------------------------
# Per-page aggregate
# ---------------------------------------------------------------------------

@dataclass
class PageStats:
    """Lightweight per-page (VPN) accumulator."""
    reads: int = 0
    writes: int = 0
    first_idx: int = 0
    last_idx: int = 0


# ---------------------------------------------------------------------------
# Region dataclass
# ---------------------------------------------------------------------------

@dataclass
class Region:
    """One inferred candidate VMA region."""
    start_vpn: int
    end_vpn: int          # *inclusive* – the last VPN that belongs to the run
    page_size: int

    # Populated by compute_features():
    num_pages_span: int = 0       # end_vpn - start_vpn + 1
    num_pages_touched: int = 0
    touch_density: float = 0.0
    reads: int = 0
    writes: int = 0
    write_frac: float = 0.0
    first_touch_ts: int = 0       # earliest first_idx among pages
    last_touch_ts: int = 0        # latest last_idx among pages
    label: str = "unknown"

    # Derived addresses
    @property
    def start_addr(self) -> int:
        return self.start_vpn * self.page_size

    @property
    def end_addr(self) -> int:
        """Exclusive end address (one byte past last page)."""
        return (self.end_vpn + 1) * self.page_size


# ---------------------------------------------------------------------------
# Pass 1 – build per-page stats
# ---------------------------------------------------------------------------

def build_page_stats(
    accesses: Generator[MemAccess, None, None],
) -> Dict[int, PageStats]:
    """Stream *accesses* and return a dict ``{vpn: PageStats}``."""
    # We deliberately do NOT materialise the whole generator – we consume it
    # one element at a time so we never hold more than O(unique-VPNs) memory.
    # The caller's page_size is baked into the VPN *before* this function is
    # called, so we receive VPNs via the ``addr`` divided outside.
    # Actually — to keep the API clean we pass raw accesses and do the
    # division here?  No — let's keep it outside so trace_reader stays generic.
    #
    # This function assumes the caller already converted addr → vpn.
    raise NotImplementedError("Use build_page_stats_from_trace instead")


def build_page_stats_from_trace(
    accesses,  # Generator / iterable of MemAccess
    page_size: int = 4096,
) -> Dict[int, PageStats]:
    """Build per-VPN stats from raw ``MemAccess`` records."""
    stats: Dict[int, PageStats] = {}
    for acc in accesses:
        vpn = acc.addr // page_size
        ps = stats.get(vpn)
        if ps is None:
            ps = PageStats(first_idx=acc.rec_idx, last_idx=acc.rec_idx)
            stats[vpn] = ps
        if acc.is_write:
            ps.writes += 1
        else:
            ps.reads += 1
        if acc.rec_idx < ps.first_idx:
            ps.first_idx = acc.rec_idx
        if acc.rec_idx > ps.last_idx:
            ps.last_idx = acc.rec_idx
    return stats


# ---------------------------------------------------------------------------
# Pass 2 – merge runs
# ---------------------------------------------------------------------------

def _merge_runs(
    sorted_vpns: List[int],
    gap_pages: int,
) -> List[Tuple[int, int]]:
    """Merge sorted VPNs into (start_vpn, end_vpn) runs.

    Two consecutive VPNs are in the same run when
    ``next_vpn - prev_vpn <= gap_pages + 1``.
    """
    if not sorted_vpns:
        return []

    runs: List[Tuple[int, int]] = []
    run_start = sorted_vpns[0]
    prev = sorted_vpns[0]
    for vpn in sorted_vpns[1:]:
        if vpn - prev > gap_pages + 1:
            runs.append((run_start, prev))
            run_start = vpn
        prev = vpn
    runs.append((run_start, prev))
    return runs


# ---------------------------------------------------------------------------
# Pass 2b – optional splitting
# ---------------------------------------------------------------------------

def _split_by_first_touch(
    vpns: List[int],
    stats: Dict[int, PageStats],
    threshold: int,
) -> List[List[int]]:
    """Split *vpns* whenever first_idx jumps by more than *threshold*."""
    if not vpns:
        return []
    groups: List[List[int]] = [[vpns[0]]]
    for vpn in vpns[1:]:
        prev_vpn = groups[-1][-1]
        prev_ft = stats[prev_vpn].first_idx
        curr_ft = stats[vpn].first_idx
        if abs(curr_ft - prev_ft) > threshold:
            groups.append([vpn])
        else:
            groups[-1].append(vpn)
    return groups


def _write_frac_in_window(
    vpns: List[int],
    stats: Dict[int, PageStats],
    center: int,
    half_window: int,
) -> float:
    lo = max(0, center - half_window)
    hi = min(len(vpns), center + half_window + 1)
    r = w = 0
    for i in range(lo, hi):
        ps = stats[vpns[i]]
        r += ps.reads
        w += ps.writes
    total = r + w
    return w / total if total else 0.0


def _split_by_write_frac(
    vpns: List[int],
    stats: Dict[int, PageStats],
    wf_window: int = 64,
    wf_split_delta: float = 0.2,
) -> List[List[int]]:
    """Split *vpns* where the write fraction changes sharply."""
    if len(vpns) < 2:
        return [vpns] if vpns else []
    half = wf_window // 2
    groups: List[List[int]] = [[vpns[0]]]
    prev_wf = _write_frac_in_window(vpns, stats, 0, half)
    for i in range(1, len(vpns)):
        cur_wf = _write_frac_in_window(vpns, stats, i, half)
        if abs(cur_wf - prev_wf) > wf_split_delta:
            groups.append([vpns[i]])
        else:
            groups[-1].append(vpns[i])
        prev_wf = cur_wf
    return groups


def _apply_splits(
    run_vpns: List[int],
    stats: Dict[int, PageStats],
    time_split_units: int,
    wf_window: int,
    wf_split_delta: float,
) -> List[List[int]]:
    """Apply first-touch and write-frac splitting to one merged run."""
    # First split by first-touch time (skip if disabled via <= 0)
    if time_split_units > 0:
        groups = _split_by_first_touch(run_vpns, stats, time_split_units)
    else:
        groups = [run_vpns]
    # …then split each group by write-frac.
    final: List[List[int]] = []
    for grp in groups:
        final.extend(_split_by_write_frac(grp, stats, wf_window, wf_split_delta))
    return final


# ---------------------------------------------------------------------------
# Feature computation
# ---------------------------------------------------------------------------

def _compute_region(
    vpns: List[int],
    stats: Dict[int, PageStats],
    page_size: int,
) -> Region:
    start_vpn = vpns[0]
    end_vpn = vpns[-1]
    r = Region(start_vpn=start_vpn, end_vpn=end_vpn, page_size=page_size)
    r.num_pages_span = end_vpn - start_vpn + 1
    r.num_pages_touched = len(vpns)
    r.touch_density = r.num_pages_touched / r.num_pages_span if r.num_pages_span else 0.0

    total_r = total_w = 0
    first_t = None
    last_t = 0
    for vpn in vpns:
        ps = stats[vpn]
        total_r += ps.reads
        total_w += ps.writes
        if first_t is None or ps.first_idx < first_t:
            first_t = ps.first_idx
        if ps.last_idx > last_t:
            last_t = ps.last_idx
    r.reads = total_r
    r.writes = total_w
    r.write_frac = total_w / (total_r + total_w) if (total_r + total_w) else 0.0
    r.first_touch_ts = first_t if first_t is not None else 0
    r.last_touch_ts = last_t
    return r


# ---------------------------------------------------------------------------
# Classification heuristics
# ---------------------------------------------------------------------------

def _classify(region: Region, all_regions: List[Region]) -> str:
    """Assign a best-effort heuristic label.  These are **guesses**."""
    if not all_regions:
        return "unknown"

    max_addr = max(r.end_addr for r in all_regions)
    min_addr = min(r.start_addr for r in all_regions)
    addr_range = max_addr - min_addr if max_addr > min_addr else 1

    # "top 10 %" of the address space → may be stack
    high_threshold = min_addr + 0.90 * addr_range
    is_high = region.start_addr >= high_threshold

    # Earliest first-touch among all regions
    earliest = min(r.first_touch_ts for r in all_regions)
    latest = max(r.last_touch_ts for r in all_regions)
    ts_range = latest - earliest if latest > earliest else 1

    rel_first_touch = (region.first_touch_ts - earliest) / ts_range

    if is_high and region.touch_density > 0.5 and region.write_frac > 0.1:
        return "stack_candidate"
    if region.write_frac > 0.2 and rel_first_touch > 0.3:
        return "rw_anon_candidate"
    if region.write_frac < 0.01 and rel_first_touch < 0.2:
        return "ro_or_file_candidate"
    # Nothing matched confidently.
    return "unknown"


# ---------------------------------------------------------------------------
# Top-level entry point
# ---------------------------------------------------------------------------

def infer_regions(
    accesses,  # iterable of MemAccess
    *,
    page_size: int = 4096,
    gap_pages: int = 4,
    min_pages: int = 8,
    time_split_units: int = 50,
    wf_window: int = 64,
    wf_split_delta: float = 0.2,
) -> List[Region]:
    """Run the full two-pass VMA inference pipeline.

    Parameters
    ----------
    accesses         : iterable of ``MemAccess`` (from ``trace_reader``).
    page_size        : virtual page size in bytes.
    gap_pages        : max gap (in pages) allowed when merging runs.
    min_pages        : discard regions smaller than this many *touched* pages.
    time_split_units : split threshold for first-touch discontinuity
                       (in record-index units).
    wf_window        : sliding window size (pages) for write-frac split.
    wf_split_delta   : min write-frac change to trigger a split.

    Returns
    -------
    List of ``Region``, sorted by ``start_addr``.
    """
    # --- Pass 1 ---
    stats = build_page_stats_from_trace(accesses, page_size=page_size)
    if not stats:
        return []

    # --- Pass 2 ---
    sorted_vpns = sorted(stats.keys())
    runs = _merge_runs(sorted_vpns, gap_pages)

    # Collect VPNs belonging to each run (we need the list for splitting).
    all_regions: List[Region] = []
    vpn_set = set(sorted_vpns)  # for O(1) membership

    for run_start, run_end in runs:
        # Gather the actual touched VPNs inside [run_start, run_end].
        run_vpns = [v for v in sorted_vpns if run_start <= v <= run_end]

        # Optional splits
        sub_groups = _apply_splits(
            run_vpns, stats, time_split_units, wf_window, wf_split_delta
        )

        for grp in sub_groups:
            if len(grp) < min_pages:
                continue
            region = _compute_region(grp, stats, page_size)
            all_regions.append(region)

    # Classify
    for region in all_regions:
        region.label = _classify(region, all_regions)

    all_regions.sort(key=lambda r: r.start_addr)
    return all_regions
