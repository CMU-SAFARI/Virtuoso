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
python3 "$HERE/parse/$PARSER" --results-dir "$RESULTS" $PARGS --md "$MD" || exit 1
echo
echo "=== table ($MD) ==="; cat "$MD"
echo
echo "plotting -> $PDF"
case "$SUITE" in
  utilsweep)
    # speedup vs memory occupancy, one line per Revelator variant
    python3 "$HERE/plot/plot_utilsweep.py" --md "$MD" --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
  multicore)
    # bar chart of the aggregate-IPC column (col 2 = aggIPC)
    python3 "$HERE/plot/plot_suite.py" --md "$MD" --out "$PDF" --col 2 && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
  *)
    # head-to-head suites: bar chart of the speedup column
    python3 "$HERE/plot/plot_suite.py" --md "$MD" --out "$PDF" && echo "  figure: $PDF" \
      || echo "  (plot skipped — need matplotlib: pip install matplotlib)"
    ;;
esac
# every suite also gets a rendered table image next to the markdown
python3 "$HERE/plot/plot_table.py" --md "$MD" --out "$AE_OUT/${FIGFILE}_table.pdf" >/dev/null 2>&1 \
  && echo "  table image: $AE_OUT/${FIGFILE}_table.pdf" || true
