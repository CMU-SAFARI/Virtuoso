#!/bin/bash
# ===========================================================================
# ae_launch.sh — PHASE 2: launch the experiments for a claim (non-blocking).
#
#   experiments/ae/ae_launch.sh --claim <claim> [--mode slurm|local]
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
#   experiments/ae/ae_watch.sh --claim <claim>
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib/ae_common.sh"
source "$HERE/lib/jobtools.sh"

CLAIM=""; MODE="slurm"; PARTS=""; AE_EXCLUDE=""; JOBS=$(( $(nproc) - 2 )); NO_PREFLIGHT=0; ICOUNT=""
while [ $# -gt 0 ]; do case "$1" in
  --claim) CLAIM="$2"; shift 2;;
  --mode) MODE="$2"; shift 2;;
  --partitions) PARTS="$2"; shift 2;;
  --exclude) AE_EXCLUDE="$2"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --icount) ICOUNT="$2"; shift 2;;
  --no-preflight) NO_PREFLIGHT=1; shift;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ -n "$CLAIM" ] || { echo "ERROR: --claim required"; exit 2; }
ae_claim_cfg "$CLAIM" || exit 1
[ -x "$ROOT/simulator/sniper/lib/sniper" ] || { echo "ERROR: lib/sniper not built (run reproduce.sh first)."; exit 1; }
[ "$JOBS" -lt 1 ] 2>/dev/null && JOBS=1
mkdir -p "$AE_OUT"

echo "==== [launch] claim=$CLAIM  mode=$MODE ===="
echo "generating jobfile ..."
ae_generate "$CLAIM" || { echo "generation failed"; exit 1; }
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
LAUNCH="$AE_OUT/$CLAIM.launch"
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
  echo "starting local run pool ($JOBS at a time), detached ..."
  # strip sbatch wrapper -> bare run-sniper commands (NUL-separated), run JOBS at
  # a time in a detached background pool. jobfile_to_cmds.py emits NUL-separated
  # commands; the grep fallback is NUL-terminated to match, so `xargs -0` is safe.
  cmds=$(mktemp)
  python3 "$HERE/lib/jobfile_to_cmds.py" "$JOBFILE" > "$cmds" 2>/dev/null
  [ -s "$cmds" ] || grep -oE '/[^ ]*run-sniper[^"]*' "$JOBFILE" | tr '\n' '\0' > "$cmds"
  setsid -f bash -c "xargs -a '$cmds' -0 -n1 -P $JOBS bash -c >/dev/null 2>&1; rm -f '$cmds'"
  echo "  local pool started."
fi

# --- record launch state for the watcher ------------------------------------
{
  echo "claim=$CLAIM"
  echo "mode=$MODE"
  echo "expected=$EXPECTED"
  echo "results=$RESULTS"
  echo "jobfile=$JOBFILE"
  echo "partitions=$PARTS"
  echo "user=$USER"
} > "$LAUNCH"
echo
echo "==== launched. Now start the watcher (backgrounds itself): ===="
echo "  bash experiments/ae/ae_watch.sh --claim $CLAIM"
