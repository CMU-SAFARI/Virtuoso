#!/bin/bash
# ===========================================================================
# ae_results.sh — PHASE 4: parse + plot a finished claim.
#
#   experiments/ae/ae_results.sh --claim <claim> [--wait]
#
# Requires the watcher's green signal (experiments/ae/ae_out/<claim>.DONE).
# With --wait it blocks until that flag appears; otherwise it errors if the
# claim is not finished yet.  Produces:
#
#   experiments/ae/ae_out/<claim>.md     table (ours vs paper)
#   experiments/ae/ae_out/<claim>.pdf    figure
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib/ae_common.sh"

CLAIM=""; WAIT=0
while [ $# -gt 0 ]; do case "$1" in
  --claim) CLAIM="$2"; shift 2;;
  --wait) WAIT=1; shift;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ -n "$CLAIM" ] || { echo "ERROR: --claim required"; exit 2; }
ae_claim_cfg "$CLAIM" || exit 1
DONEF="$AE_OUT/$CLAIM.DONE"; STATUS="$AE_OUT/$CLAIM.status"

if [ ! -f "$DONEF" ]; then
  if [ "$WAIT" -eq 1 ]; then
    echo "waiting for the watcher's green signal ($DONEF) ..."
    until [ -f "$DONEF" ]; do sleep 30; done
  else
    echo "ERROR: $CLAIM not finished yet (no $DONEF)."
    [ -f "$STATUS" ] && { echo "current status:"; sed 's/^/  /' "$STATUS"; }
    echo "Re-run with --wait to block until it finishes."
    exit 1
  fi
fi

echo "==== [results] $CLAIM  ->  paper $FIG ===="
sed 's/^/  /' "$DONEF" | head -8
MD="$AE_OUT/$CLAIM.md"; PDF="$AE_OUT/$FIGFILE.pdf"
echo "parsing -> $MD"
python3 "$HERE/parse/$PARSER" --results-dir "$RESULTS" --top200 "$TOP200" $PARGS --md "$MD" >/dev/null 2>&1 \
  || python3 "$HERE/parse/$PARSER" --results-dir "$RESULTS" $PARGS --md "$MD"   # multicore has no --top200
echo
echo "=== table ($MD) ==="; cat "$MD"
echo
echo "plotting -> $PDF"
have_valid() { [ -d "$1" ] && [ -n "$(find "$1" -maxdepth 3 -name sim.stats -size +50k -print -quit 2>/dev/null)" ]; }
case "$CLAIM" in
  head8mb|head2mb)
    # paper-format single-core figure; include both NUCA rows if both claims ran
    args=""
    have_valid "$EXP/exp_ae_head2mb/results" && args="$args --head2mb $EXP/exp_ae_head2mb/results"
    have_valid "$EXP/exp_ae_head8mb/results" && args="$args --head8mb $EXP/exp_ae_head8mb/results"
    python3 "$HERE/plot/plot_singlecore.py" $args --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
  multicore)
    # paper-format multicore figure (equal-work harmonic-mean from heartbeats)
    python3 "$HERE/plot/plot_multicore.py" --results-dir "$RESULTS" --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
  pqsweep)
    # Figure 20: TRAIL-vs-ASP speedup across PQ sizes (line plot)
    python3 "$HERE/plot/plot_pqsweep.py" --results-dir "$RESULTS" --top200 "$TOP200" --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
  table5|table6)
    # render the parsed table as an image
    python3 "$HERE/plot/plot_table.py" --md "$MD" --out "$PDF" && echo "  table image: $PDF" \
      || echo "  (render skipped — need matplotlib: pip install matplotlib)"
    ;;
  *)
    python3 "$HERE/plot/plot_claim.py" --md "$MD" --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
esac
