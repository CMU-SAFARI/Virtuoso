#!/bin/bash
# ===========================================================================
# ae_run_all.sh — launch + watch MULTIPLE claims in parallel.
#
#   experiments/ae/ae_run_all.sh [--claims "head8mb table5 ..." | all]
#         [--mode slurm|local] [--partitions cpu_part,bio_part]
#         [--exclude node01,...] [--jobs N]
#
# For each claim it runs ae_launch.sh (submit, non-blocking) then ae_watch.sh
# (self-detaching background watcher). All claims proceed independently — each
# has its own exp dir, its own ae_out/<claim>.{status,DONE}, and job names that
# never collide, so the watchers don't interfere.
#
# Combined views (run any time):
#   experiments/ae/ae_run_all.sh --status     # progress of every launched claim
#   experiments/ae/ae_run_all.sh --results    # parse + plot every FINISHED claim
#
# Paper mapping:  head8mb+head2mb -> Figure 12 (8MB bottom / 2MB top),
#                 table5 -> Table 5,  table6 -> Table 6,
#                 pqsweep -> Figure 20,  multicore -> Figure 22.
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib/ae_common.sh"

CLAIMS="$ALL_CLAIMS"; MODE="slurm"; PARTS=""; EXCLUDE=""; JOBS=""; ICOUNT=""; ACTION="run"
while [ $# -gt 0 ]; do case "$1" in
  --claims) CLAIMS="$2"; shift 2;;
  --mode) MODE="$2"; shift 2;;
  --partitions) PARTS="$2"; shift 2;;
  --exclude) EXCLUDE="$2"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --icount) ICOUNT="$2"; shift 2;;
  --status) ACTION="status"; shift;;
  --results) ACTION="results"; shift;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ "$CLAIMS" = "all" ] && CLAIMS="$ALL_CLAIMS"

# --- combined status view ---------------------------------------------------
if [ "$ACTION" = "status" ]; then
  shown=0
  for c in $CLAIMS; do
    ae_claim_cfg "$c" >/dev/null 2>&1
    s="$AE_OUT/$c.status"
    [ -f "$s" ] || continue
    shown=1
    echo "================ $c  ($FIG) ================"
    cat "$s"; echo
  done
  [ "$shown" -eq 0 ] && echo "no claims launched yet (no ae_out/*.status)."
  exit 0
fi

# --- parse + plot every finished claim --------------------------------------
if [ "$ACTION" = "results" ]; then
  for c in $CLAIMS; do
    if [ -f "$AE_OUT/$c.DONE" ]; then
      bash "$HERE/ae_results.sh" --claim "$c"
    else
      echo "[skip] $c not finished yet (no ae_out/$c.DONE)."
    fi
  done
  exit 0
fi

# --- launch + watch each claim ----------------------------------------------
[ "$MODE" = "local" ] && [ "$(echo $CLAIMS | wc -w)" -gt 1 ] && \
  echo "NOTE: --mode local runs all claims' sims on THIS machine — they share CPUs. Prefer SLURM for parallel claims."

for c in $CLAIMS; do
  echo; echo "########## $c ##########"
  bash "$HERE/ae_launch.sh" --claim "$c" --mode "$MODE" \
       ${PARTS:+--partitions "$PARTS"} ${EXCLUDE:+--exclude "$EXCLUDE"} \
       ${JOBS:+--jobs "$JOBS"} ${ICOUNT:+--icount "$ICOUNT"} \
       || { echo "[skip] $c launch failed"; continue; }
  bash "$HERE/ae_watch.sh" --claim "$c"
done

echo
echo "==== all requested claims launched + watched in parallel ===="
echo "  progress (any time):   bash experiments/ae/ae_run_all.sh --status"
echo "  results when done:     bash experiments/ae/ae_run_all.sh --results"
