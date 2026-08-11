#!/bin/bash
# ===========================================================================
# ae_watch.sh — PHASE 3: watch a launched suite (runs in the BACKGROUND).
#
#   experiments/ae/ae_watch.sh --suite <suite> [--interval 60]
#
# Detaches itself and periodically writes a human-readable status file:
#
#   experiments/ae/ae_out/<suite>.status     <- `cat` this any time
#
# It reports how many jobs have finished, how many are still running, and (at
# the end) how many PASSED vs FAILED, listing the failed run-dirs. When every
# job is accounted for it writes experiments/ae/ae_out/<suite>.DONE (the green
# signal) and exits. Phase 4 (ae_results.sh) waits for that flag.
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib/ae_common.sh"
source "$HERE/lib/jobtools.sh"

SUITE=""; INTERVAL=60
while [ $# -gt 0 ]; do case "$1" in
  --suite) SUITE="$2"; shift 2;;
  --interval) INTERVAL="$2"; shift 2;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ -n "$SUITE" ] || { echo "ERROR: --suite required"; exit 2; }
ae_suite_cfg "$SUITE" || exit 1
LAUNCH="$AE_OUT/$SUITE.launch"
[ -f "$LAUNCH" ] || { echo "ERROR: $SUITE not launched (no $LAUNCH). Run ae_launch.sh first."; exit 1; }
STATUS="$AE_OUT/$SUITE.status"; DONEF="$AE_OUT/$SUITE.DONE"; WLOG="$AE_OUT/$SUITE.watch.log"

# --- self-detach into the background on first invocation --------------------
if [ "${AE_WATCH_DAEMON:-0}" != "1" ]; then
  rm -f "$DONEF"
  AE_WATCH_DAEMON=1 setsid -f bash "$0" --suite "$SUITE" --interval "$INTERVAL" >"$WLOG" 2>&1
  echo "watcher started in background for '$SUITE'."
  echo "  check progress any time:   cat $STATUS"
  echo "  when finished it writes:   $DONEF"
  exit 0
fi

# --- daemon: read launch state ----------------------------------------------
eval "$(grep -E '^(mode|expected|results|jobfile)=' "$LAUNCH")"
RUNDIRS=()
if [ "$KIND" = "mot" ]; then
  # motivation: progress is one JSON per workload, and the jobs are named mot_<wl>
  UNIT="analysed workloads"
  count_valid() { ls "$results"/*.json 2>/dev/null | wc -l; }
  count_active_slurm() {
    squeue -u "$USER" -h -o '%j' -t PD,R,CG,CF,S 2>/dev/null | grep -c '^mot_'
  }
else
  UNIT="jobs with a valid sim.stats"
  mapfile -t RUNDIRS < <(ae_expected_rundirs "$jobfile")
  # suite's slurm job names (-J), to tell when the suite's jobs are gone
  mapfile -t JOBNAMES < <(grep -oE -- '-J [^ ]+' "$jobfile" | awk '{print $2}' | sort -u)

  count_valid() {  # valid sim.stats among expected rundirs
    local d n=0; for d in "${RUNDIRS[@]}"; do ae_valid_result "$d" && n=$((n+1)); done; echo "$n"
  }
  count_active_slurm() {  # suite jobs still queued/running
    local active; active=$(comm -12 \
       <(squeue -u "$USER" -h -o '%j' -t PD,R,CG,CF,S 2>/dev/null | sort -u) \
       <(printf '%s\n' "${JOBNAMES[@]}") | wc -l); echo "$active"
  }
fi

exp="${expected:-${#RUNDIRS[@]}}"; prev_done=-1; plateau=0; idle=0
while :; do
  done=$(count_valid)
  if [ "$mode" = "slurm" ]; then active=$(count_active_slurm); else
    # local: no SLURM queue to poll, so infer activity from progress. Reset on
    # any progress; otherwise count no-progress polls. Only treat a plateau as
    # "finished" AFTER at least one job has completed (so a slow first sim is
    # not mistaken for an empty run); an absolute idle cap still stops a run
    # that is genuinely stuck. At the default 60s interval: plateau=15 (~15 min
    # with no new result after the first), idle cap=120 (~2 h total).
    if [ "$done" -ne "$prev_done" ]; then plateau=0; idle=0; else plateau=$((plateau+1)); idle=$((idle+1)); fi
    stalled=0
    { [ "$done" -gt 0 ] && [ "$plateau" -ge 15 ]; } && stalled=1
    [ "$idle" -ge 120 ] && stalled=1
    active=$([ "$done" -lt "$exp" ] && [ "$stalled" -eq 0 ] && echo 1 || echo 0)
  fi
  prev_done=$done
  pending=$(( exp - done )); [ "$pending" -lt 0 ] && pending=0
  {
    echo "suite:   $SUITE        mode: $mode"
    echo "updated: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "done:    $done / $exp   ($UNIT)"
    echo "active:  $active   (running/pending)"
    echo "status:  $([ "$active" -gt 0 ] && echo RUNNING || echo FINISHING)"
  } > "$STATUS"

  [ "$active" -eq 0 ] && break
  sleep "$INTERVAL"
done

# --- final pass/fail report -------------------------------------------------
done=$(count_valid); failed=$(( exp - done )); [ "$failed" -lt 0 ] && failed=0
{
  echo "suite:   $SUITE        mode: $mode"
  echo "updated: $(date '+%Y-%m-%d %H:%M:%S')"
  echo "status:  DONE"
  echo "passed:  $done / $exp"
  echo "failed:  $failed"
  if [ "$failed" -gt 0 ] && [ "$KIND" = "mot" ]; then
    echo "workloads with no JSON (see $results/<workload>.slurm.err):"
    ls "$results"/*.slurm.err 2>/dev/null | head -50 | sed 's|.*/|  |; s|\.slurm\.err$||'
  elif [ "$failed" -gt 0 ]; then
    echo "failed jobs (no valid sim.stats):"
    for d in "${RUNDIRS[@]}"; do
      ae_valid_result "$d" || echo "  $(basename "$d")   (see $d/slurm.err)"
    done | head -50
  fi
} | tee "$STATUS" > "$DONEF"
echo "[watch] $SUITE DONE: $done passed, $failed failed."
