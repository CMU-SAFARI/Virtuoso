#!/bin/bash
# launch.sh MODE JOBFILE [JOBS]
#   MODE  = slurm | local
#   slurm : submit every sbatch line (no throttle); returns immediately.
#   local : strip the sbatch/native_wrapper wrapper and run the raw run-sniper
#           commands directly, JOBS at a time; BLOCKS until all finish.
set -u
MODE="$1"; JOBFILE="$2"; JOBS="${3:-$(( $(nproc) - 2 ))}"
[ "$JOBS" -lt 1 ] 2>/dev/null && JOBS=1
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ ! -s "$JOBFILE" ]; then echo "launch: missing/empty jobfile $JOBFILE" >&2; exit 1; fi

case "$MODE" in
  slurm)
    echo "[launch] slurm: submitting $(grep -c '^sbatch' "$JOBFILE") jobs (no throttle)"
    n=0
    while IFS= read -r line; do
      case "$line" in sbatch*) eval "$line" >/dev/null 2>&1 && n=$((n+1));; esac
    done < "$JOBFILE"
    echo "[launch] submitted $n jobs"
    ;;
  local)
    total=$(grep -c '^sbatch' "$JOBFILE")
    echo "[launch] local: running $total jobs, $JOBS at a time (this blocks until done)"
    python3 "$HERE/jobfile_to_cmds.py" "$JOBFILE" 2>/dev/null \
      | xargs -0 -n1 -P "$JOBS" bash -c
    echo "[launch] local run complete"
    ;;
  *)
    echo "launch: unknown mode '$MODE' (use slurm|local)" >&2; exit 2;;
esac
