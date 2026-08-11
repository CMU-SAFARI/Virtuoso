#!/bin/bash
# ===========================================================================
# ae_launch.sh — PHASE 2: launch the experiments for a suite (non-blocking).
#
#   experiments/ae/ae_launch.sh --suite <suite> [--mode slurm|local]
#         [--partitions p1,p2] [--exclude node01,...] [--jobs N] [--icount N]
#
# --partitions is optional: if omitted, no --partition is passed to sbatch (your
#   cluster's default partition is used).
# --icount overrides every job's instruction budget (default 300M, 100M for the
#   virtuoso traces) — e.g. --icount 2000000 for a quick end-to-end test.
#
# Generates the jobfile, checks the traces are present, submits ALL jobs, and
# returns immediately. It does NOT wait — start the watcher next:
#
#   experiments/ae/ae_watch.sh --suite <suite>
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib/ae_common.sh"
source "$HERE/lib/jobtools.sh"

SUITE=""; MODE="slurm"; PARTS=""; AE_EXCLUDE=""; JOBS=$(( $(nproc) - 2 )); NO_PREFLIGHT=0; ICOUNT=""; EMIT_CMDS=""
while [ $# -gt 0 ]; do case "$1" in
  --suite) SUITE="$2"; shift 2;;
  --mode) MODE="$2"; shift 2;;
  --partitions) PARTS="$2"; shift 2;;
  --exclude) AE_EXCLUDE="$2"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --icount) ICOUNT="$2"; shift 2;;
  --no-preflight) NO_PREFLIGHT=1; shift;;
  # local only: write this suite's NUL-separated commands to FILE and DON'T start
  # a pool. ae_run_all.sh uses this to feed one shared scheduler across suites.
  --emit-cmds) EMIT_CMDS="$2"; shift 2;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ -n "$SUITE" ] || { echo "ERROR: --suite required"; exit 2; }
ae_suite_cfg "$SUITE" || exit 1
mkdir -p "$AE_OUT"

# The motivation "suite" has no jobfile and no simulator: hand it to its own
# driver, which submits/queues one analysis per PTW dump and writes the same
# ae_out/<suite>.launch the watcher reads. Everything downstream is unchanged.
if [ "$KIND" = "mot" ]; then
  exec bash "$HERE/motivation/run_motivation.sh" --mode "$MODE" \
       ${PARTS:+--partitions "$PARTS"} ${JOBS:+--jobs "$JOBS"} \
       ${EMIT_CMDS:+--emit-cmds "$EMIT_CMDS"}
fi

[ -x "$ROOT/simulator/sniper/lib/sniper" ] || { echo "ERROR: lib/sniper not built (run build_and_validate.sh first)."; exit 1; }
[ "$JOBS" -lt 1 ] 2>/dev/null && JOBS=1
mkdir -p "$AE_OUT"

echo "==== [launch] suite=$SUITE  mode=$MODE ===="
echo "generating jobfile ..."
ae_generate "$SUITE" || { echo "generation failed"; exit 1; }
# --icount: override every job's instruction budget (quick testing; default 300M/100M)
if [ -n "$ICOUNT" ]; then
  sed -i -E "s/stop-by-icount:[0-9]+/stop-by-icount:${ICOUNT}/g" "$JOBFILE"
  echo "  NOTE: instruction count overridden to ${ICOUNT} for all jobs (quick test)."
fi
EXPECTED=$(grep -c '^sbatch' "$JOBFILE")
echo "  $EXPECTED jobs -> $RESULTS"

if [ "$NO_PREFLIGHT" -eq 0 ]; then
  ae_preflight_traces "$JOBFILE" || { echo "[abort] preflight failed — resolve traces (setup_tlists.sh) first."; exit 1; }
fi

# --- submit -----------------------------------------------------------------
LAUNCH="$AE_OUT/$SUITE.launch"
if [ "$MODE" = "slurm" ]; then
  desc="${PARTS:+partition=$PARTS}"; desc="${desc:-default partition}"
  [ -n "$AE_EXCLUDE" ] && desc="$desc, exclude=$AE_EXCLUDE"
  echo "submitting $EXPECTED SLURM jobs ($desc) ..."
  # submit every job lacking a valid result, injecting partitions
  ntmp=$(mktemp); ae_missing_lines "$JOBFILE" "$PARTS" > "$ntmp"
  cnt=0
  while IFS= read -r -d '' line; do eval "$line" >/dev/null 2>&1 && cnt=$((cnt+1)); done < "$ntmp"
  rm -f "$ntmp"
  echo "  submitted $cnt jobs."
else
  # strip sbatch wrapper -> bare run-sniper commands (NUL-separated). jobfile_to_cmds.py
  # emits NUL-separated commands; the grep fallback is NUL-terminated to match.
  cmds=$(mktemp)
  python3 "$HERE/lib/jobfile_to_cmds.py" "$JOBFILE" > "$cmds" 2>/dev/null
  [ -s "$cmds" ] || grep -oE '/[^ ]*run-sniper[^"]*' "$JOBFILE" | tr '\n' '\0' > "$cmds"
  if [ -n "$EMIT_CMDS" ]; then
    # feed a shared scheduler (ae_run_all): hand off the command list, no pool here.
    cp "$cmds" "$EMIT_CMDS"; rm -f "$cmds"
    echo "  local: queued $EXPECTED jobs for the shared scheduler."
  else
    echo "starting local run pool ($JOBS at a time), detached ..."
    # run JOBS at a time in a detached background pool (`xargs -0` matches the NUL list).
    setsid -f bash -c "xargs -a '$cmds' -0 -n1 -P $JOBS bash -c >/dev/null 2>&1; rm -f '$cmds'"
    echo "  local pool started."
  fi
fi

# --- record launch state for the watcher ------------------------------------
{
  echo "suite=$SUITE"
  echo "mode=$MODE"
  echo "expected=$EXPECTED"
  echo "results=$RESULTS"
  echo "jobfile=$JOBFILE"
  echo "partitions=$PARTS"
  echo "user=$USER"
} > "$LAUNCH"
echo
echo "==== launched. Now start the watcher (backgrounds itself): ===="
echo "  bash experiments/ae/ae_watch.sh --suite $SUITE"
