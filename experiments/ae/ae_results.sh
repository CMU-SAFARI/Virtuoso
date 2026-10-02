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
#   experiments/ae/ae_out/<figure>.md    the figure's numbers
#   experiments/ae/ae_out/<figure>.pdf   the figure (and a .png)
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

# motivation: no parser/table — its own driver renders the five figures
if [ "$KIND" = "mot" ]; then
  exec bash "$HERE/motivation/run_motivation.sh" --plot
fi
MD="$AE_OUT/$FIGFILE.md"; PDF="$AE_OUT/$FIGFILE.pdf"
FIGS="$HERE/plot/paper_figures.py"
have_valid() { [ -d "$1" ] && [ -n "$(find "$1" -maxdepth 3 -name sim.stats -size +50k -print -quit 2>/dev/null)" ]; }
H2="$EXP/exp_ae_head2mb/results"; H8="$EXP/exp_ae_head8mb/results"
case "$SUITE" in
  head8mb|head2mb)
    # Figure 11 has one row per LLC size: drawn with whichever suites have results
    args=""
    have_valid "$H2" && args="$args --head2mb $H2"
    have_valid "$H8" && args="$args --head8mb $H8"
    python3 "$FIGS" figure11 $args --out "$PDF" --md "$MD"
    if [ "$SUITE" = head2mb ]; then
      python3 "$FIGS" figure13 --head2mb "$H2" --out "$AE_OUT/figure13.pdf" --md "$AE_OUT/figure13.md"
    fi
    ;;
  table5) python3 "$FIGS" table4 --results "$RESULTS" --head8mb "$H8" --out "$PDF" --md "$MD";;
  table6) python3 "$FIGS" table5 --results "$RESULTS" --head8mb "$H8" --out "$PDF" --md "$MD";;
  multicore) python3 "$FIGS" figure16 --multicore "$RESULTS" --out "$PDF" --md "$MD";;
  abl2mb) python3 "$FIGS" figure19 --abl "$RESULTS" --head2mb "$H2" --top200 "$TOP200" --out "$PDF" --md "$MD";;
  head2mb_mtps*)
    # Figure 18: every DRAM speed whose suite has results (2400 MT/s is head2mb)
    pts=""
    for m in $MTPS_VALUES; do
      r="$EXP/exp_ae_head2mb_mtps$m/results"; have_valid "$r" && pts="$pts $m=$r"
    done
    python3 "$FIGS" figure18 --head2mb "$H2" --mtps $pts --top200 "$TOP200" --out "$PDF" --md "$MD";;
esac
