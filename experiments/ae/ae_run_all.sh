#!/bin/bash
# ===========================================================================
# ae_run_all.sh — launch + watch MULTIPLE suites in parallel.
#
#   experiments/ae/ae_run_all.sh [--suites "head8mb table5 ..." | all]
#         [--mode slurm|local] [--partitions cpu_part,bio_part]
#         [--exclude node01,...] [--jobs N]
#
# For each suite it runs ae_launch.sh (submit, non-blocking) then ae_watch.sh
# (self-detaching background watcher). All suites proceed independently — each
# has its own exp dir, its own ae_out/<suite>.{status,DONE}, and job names that
# never collide, so the watchers don't interfere.
#
# Combined views (run any time):
#   experiments/ae/ae_run_all.sh --status     # progress of every launched suite
#   experiments/ae/ae_run_all.sh --results    # parse + plot every FINISHED suite
#
# Paper mapping:  head8mb+head2mb -> Figure 12 (8MB bottom / 2MB top),
#                 table5 -> Table 5,  table6 -> Table 6,
#                 pqsweep -> Figure 20,  multicore -> Figure 22.
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib/ae_common.sh"

SUITES="$ALL_SUITES"; MODE="slurm"; PARTS=""; EXCLUDE=""; JOBS=""; ICOUNT=""; ACTION="run"
while [ $# -gt 0 ]; do case "$1" in
  --suites) SUITES="$2"; shift 2;;
  --mode) MODE="$2"; shift 2;;
  --partitions) PARTS="$2"; shift 2;;
  --exclude) EXCLUDE="$2"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --icount) ICOUNT="$2"; shift 2;;
  --status) ACTION="status"; shift;;
  --results) ACTION="results"; shift;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ "$SUITES" = "all" ] && SUITES="$ALL_SUITES"

# --- combined status view ---------------------------------------------------
if [ "$ACTION" = "status" ]; then
  shown=0
  for c in $SUITES; do
    ae_suite_cfg "$c" >/dev/null 2>&1
    s="$AE_OUT/$c.status"
    [ -f "$s" ] || continue
    shown=1
    echo "================ $c  ($FIG) ================"
    cat "$s"; echo
  done
  [ "$shown" -eq 0 ] && echo "no suites launched yet (no ae_out/*.status)."
  exit 0
fi

# --- parse + plot every finished suite --------------------------------------
if [ "$ACTION" = "results" ]; then
  for c in $SUITES; do
    if [ -f "$AE_OUT/$c.DONE" ]; then
      bash "$HERE/ae_results.sh" --suite "$c"
    else
      echo "[skip] $c not finished yet (no ae_out/$c.DONE)."
    fi
  done
  exit 0
fi

# --- launch + watch each suite ----------------------------------------------
if [ "$MODE" = "local" ]; then
  # ONE shared scheduler across every requested suite: collect all suites' jobs
  # into a single pool so at most N simulations run at once no matter how many
  # suites you pick (N = --jobs, default cores-2). No need to run suites one at a
  # time. Each suite still gets its own watcher and produces its own figure/table.
  SJOBS=${JOBS:-$(( $(nproc) - 2 ))}; [ "$SJOBS" -lt 1 ] 2>/dev/null && SJOBS=1
  tmpd=$(mktemp -d); files=()
  for c in $SUITES; do
    echo; echo "########## $c ##########"
    if bash "$HERE/ae_launch.sh" --suite "$c" --mode local --emit-cmds "$tmpd/$c.cmds" \
         ${EXCLUDE:+--exclude "$EXCLUDE"} ${ICOUNT:+--icount "$ICOUNT"}; then
      [ -s "$tmpd/$c.cmds" ] && files+=("$tmpd/$c.cmds")
      bash "$HERE/ae_watch.sh" --suite "$c"
    else echo "[skip] $c launch failed"; fi
  done
  if [ "${#files[@]}" -eq 0 ]; then echo "[abort] no jobs to run."; rm -rf "$tmpd"; exit 1; fi
  # round-robin interleave the suites' command lists so every suite makes progress
  # early (its watcher sees results and won't time out waiting behind other suites).
  combined=$(mktemp)
  python3 - "$combined" "${files[@]}" <<'PY'
import sys
out = open(sys.argv[1], "wb")
lists = [[x for x in open(f, "rb").read().split(b"\0") if x] for f in sys.argv[2:]]
i = 0
while any(i < len(l) for l in lists):
    for l in lists:
        if i < len(l): out.write(l[i] + b"\0")
    i += 1
out.close()
PY
  rm -rf "$tmpd"
  total=$(tr -cd '\0' < "$combined" | wc -c)
  echo; echo "==== shared local scheduler: $total simulations, $SJOBS at a time (detached) ===="
  setsid -f bash -c "xargs -a '$combined' -0 -n1 -P $SJOBS bash -c >/dev/null 2>&1; rm -f '$combined'"
else
  for c in $SUITES; do
    echo; echo "########## $c ##########"
    bash "$HERE/ae_launch.sh" --suite "$c" --mode "$MODE" \
         ${PARTS:+--partitions "$PARTS"} ${EXCLUDE:+--exclude "$EXCLUDE"} \
         ${JOBS:+--jobs "$JOBS"} ${ICOUNT:+--icount "$ICOUNT"} \
         || { echo "[skip] $c launch failed"; continue; }
    bash "$HERE/ae_watch.sh" --suite "$c"
  done
fi

echo
echo "==== all requested suites launched + watched ===="
echo "  progress (any time):   bash experiments/ae/ae_run_all.sh --status"
echo "  results when done:     bash experiments/ae/ae_run_all.sh --results"
