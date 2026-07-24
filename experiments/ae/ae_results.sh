#!/bin/bash
# ===========================================================================
# ae_results.sh — PHASE 4: parse + plot a finished suite.
#
#   experiments/ae/ae_results.sh --suite <suite> [--wait]
#
# Requires the watcher's green signal (experiments/ae/ae_out/<suite>.DONE).
# With --wait it blocks until that flag appears; otherwise it errors if the
# suite is not finished yet.  Produces:
#
#   experiments/ae/ae_out/<suite>.md     table (ours vs paper)
#   experiments/ae/ae_out/<suite>.pdf    figure
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib/ae_common.sh"

SUITE=""; WAIT=0
while [ $# -gt 0 ]; do case "$1" in
  --suite) SUITE="$2"; shift 2;;
  --wait) WAIT=1; shift;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ -n "$SUITE" ] || { echo "ERROR: --suite required"; exit 2; }
ae_suite_cfg "$SUITE" || exit 1
DONEF="$AE_OUT/$SUITE.DONE"; STATUS="$AE_OUT/$SUITE.status"

if [ ! -f "$DONEF" ]; then
  if [ "$WAIT" -eq 1 ]; then
    echo "waiting for the watcher's green signal ($DONEF) ..."
    until [ -f "$DONEF" ]; do sleep 30; done
  else
    echo "ERROR: $SUITE not finished yet (no $DONEF)."
    [ -f "$STATUS" ] && { echo "current status:"; sed 's/^/  /' "$STATUS"; }
    echo "Re-run with --wait to block until it finishes."
    exit 1
  fi
fi

echo "==== [results] $SUITE  ->  paper $FIG ===="
sed 's/^/  /' "$DONEF" | head -8
MD="$AE_OUT/$SUITE.md"; PDF="$AE_OUT/$FIGFILE.pdf"
echo "parsing -> $MD"
python3 "$HERE/parse/$PARSER" --results-dir "$RESULTS" --top200 "$TOP200" $PARGS --md "$MD" >/dev/null 2>&1 \
  || python3 "$HERE/parse/$PARSER" --results-dir "$RESULTS" $PARGS --md "$MD"   # multicore has no --top200
echo
echo "=== table ($MD) ==="; cat "$MD"
echo
echo "plotting -> $PDF"
have_valid() { [ -d "$1" ] && [ -n "$(find "$1" -maxdepth 3 -name sim.stats -size +50k -print -quit 2>/dev/null)" ]; }
case "$SUITE" in
  head8mb|head2mb)
    # paper-format single-core figure; include both NUCA rows if both suites ran
    args=""
    have_valid "$EXP/exp_ae_head2mb/results" && args="$args --head2mb $EXP/exp_ae_head2mb/results"
    have_valid "$EXP/exp_ae_head8mb/results" && args="$args --head8mb $EXP/exp_ae_head8mb/results"
    python3 "$HERE/plot/plot_singlecore.py" $args --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    # Figure 13: TRAIL mechanism metrics, from the 8 MB single-core results
    if have_valid "$EXP/exp_ae_head8mb/results"; then
      python3 "$HERE/plot/plot_mechanism.py" --results-dir "$EXP/exp_ae_head8mb/results" \
        --out "$AE_OUT/figure13.pdf" && echo "  figure: $AE_OUT/figure13.pdf (mechanism)" || true
    fi
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
    python3 "$HERE/plot/plot_suite.py" --md "$MD" --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
esac
