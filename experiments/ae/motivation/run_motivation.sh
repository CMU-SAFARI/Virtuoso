#!/bin/bash
# ===========================================================================
# run_motivation.sh — run the PTW-dump analysis over all workloads, then plot
# the motivation figures. Choose SLURM (one job per workload) or local (a pool
# of up to N cores).
#
#   experiments/ae/motivation/run_motivation.sh --dumps <dir> [--mode local|slurm]
#         [--jobs N] [--partitions p1,p2] [--out <dir>]
#
#   --dumps       directory of downloaded dumps (ptw_dumps/*.csv[.gz])
#   --mode local  run the analysis on this machine, up to --jobs cores (default:
#                 all-but-two). --mode slurm submits one job per workload.
#   --out         where per-workload JSON + the figures go (default: motivation_out/)
#
# Resumable: workloads whose JSON already exists are skipped. After the analysis
# completes it runs plot_motivation.py to produce the figures.
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../lib/venv.sh"                  # put the AE Python venv on PATH (numpy/matplotlib)
DUMPS=""; MODE="local"; JOBS=$(( $(nproc) - 2 )); PARTS=""; OUT="$HERE/motivation_out"
while [ $# -gt 0 ]; do case "$1" in
  --dumps) DUMPS="$2"; shift 2;;
  --mode) MODE="$2"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --partitions) PARTS="$2"; shift 2;;
  --out) OUT="$2"; shift 2;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ -n "$DUMPS" ] || { echo "ERROR: --dumps <dir> required (the downloaded ptw_dumps/)"; exit 2; }
# accept either the dataset root or the ptw_dumps/ subdir
[ -d "$DUMPS/ptw_dumps" ] && DUMPS="$DUMPS/ptw_dumps"
ls "$DUMPS"/*.csv* >/dev/null 2>&1 || { echo "ERROR: no dumps (*.csv/*.csv.gz) in $DUMPS"; exit 1; }
[ "$JOBS" -lt 1 ] 2>/dev/null && JOBS=1
JDIR="$OUT/json"; mkdir -p "$JDIR"
AN="$HERE/analyze_dump.py"

# build the worklist of dumps still needing a JSON
work=$(mktemp)
for dump in "$DUMPS"/*.csv "$DUMPS"/*.csv.gz; do
  [ -e "$dump" ] || continue
  wl=$(basename "$dump"); wl="${wl%.csv.gz}"; wl="${wl%.csv}"
  [ -f "$JDIR/$wl.json" ] || echo "$dump"
done > "$work"
todo=$(grep -c . "$work"); total=$(ls "$DUMPS"/*.csv "$DUMPS"/*.csv.gz 2>/dev/null | wc -l)
echo "==== motivation analysis: $todo of $total workloads to run (mode=$MODE) ===="

if [ "$todo" -gt 0 ]; then
  if [ "$MODE" = "slurm" ]; then
    while IFS= read -r dump; do
      wl=$(basename "$dump"); wl="${wl%.csv.gz}"; wl="${wl%.csv}"
      sbatch ${PARTS:+--partition=$PARTS} -c 2 -J "mot_$wl" \
        --output="$JDIR/$wl.slurm.out" --error="$JDIR/$wl.slurm.err" \
        --wrap="python3 '$AN' --dump '$dump' --workload '$wl' --out '$JDIR/$wl.json'" >/dev/null
    done < "$work"
    echo "  submitted $todo SLURM jobs. Wait for them, then re-run this script to plot."
    echo "  (check: ls $JDIR/*.json | wc -l   should reach $total)"
    # if not all done yet, stop here; a re-run will plot once complete
    [ "$(ls "$JDIR"/*.json 2>/dev/null | wc -l)" -lt "$total" ] && { rm -f "$work"; exit 0; }
  else
    echo "  running locally on up to $JOBS cores ..."
    xargs -a "$work" -P "$JOBS" -I{} bash -c '
      d="{}"; wl=$(basename "$d"); wl="${wl%.csv.gz}"; wl="${wl%.csv}"
      python3 "'"$AN"'" --dump "$d" --workload "$wl" --out "'"$JDIR"'/$wl.json" >/dev/null 2>&1'
  fi
fi
rm -f "$work"

have=$(ls "$JDIR"/*.json 2>/dev/null | wc -l)
echo "  analyzed: $have/$total workloads."
echo "==== plotting motivation figures -> $OUT ===="
python3 "$HERE/plot_motivation.py" --json-dir "$JDIR" --dumps-dir "$DUMPS" --out-dir "$OUT" \
  && echo "figures in $OUT" || echo "  (plot failed — need matplotlib)"
