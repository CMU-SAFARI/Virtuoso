#!/bin/bash
# ===========================================================================
# run_motivation.sh — analyse the PTW dumps, then plot the motivation figures.
#
# TWO STEPS, two commands. The analysis is one job per workload (251 of them);
# in SLURM mode it only submits, so the figures cannot exist until those jobs
# finish. Plotting is therefore its own command in BOTH modes — the same two
# commands work on a cluster and on a laptop.
#
#   0) get dumps: run_motivation.sh --download        (only if none are staged)
#   1) analyse:   run_motivation.sh [--mode local|slurm] [--jobs N] [--partitions p]
#   2) plot:      run_motivation.sh --plot
#
# Step 1 is the default action; it never plots and ends by printing step 2.
# Step 2 refuses to draw incomplete figures unless you pass --allow-partial.
#
# Step 0 exists so you never have to activate the venv: `hf` lives in the AE
# virtualenv, which lib/venv.sh puts on PATH inside these scripts but NOT in your
# interactive shell. Running the download through this script means every command
# in the artifact is a harness command.
#
#   experiments/ae/motivation/run_motivation.sh [--download | --plot] [--dumps <dir>]
#         [--mode local|slurm] [--jobs N] [--partitions p1,p2] [--out <dir>]
#         [--allow-partial] [--hf-repo REPO]
#
#   --dumps       directory of downloaded dumps (ptw_dumps/*.csv[.gz]). Optional:
#                 if omitted, a PRE-STAGED bundle is looked for, in this order:
#                     $HOME/ptw_bundle
#                     <artifact>/../ptw_bundle
#                     <artifact>/ptw_bundle
#                 This mirrors the trace bundle handled by build_and_validate.sh,
#                 so on a machine where the dumps were fetched once and shared
#                 between accounts (typically a symlink into a common directory)
#                 the 2.9 GB download is skipped entirely.
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
ROOT="$(cd "$HERE/../../.." && pwd)"           # artifact root (…/Virtuoso)
DUMPS=""; MODE="local"; JOBS=$(( $(nproc) - 2 )); PARTS=""; OUT="$HERE/motivation_out"
ACTION="analyse"; PARTIAL=0; OUT_SET=""; HF_REPO="${HF_REPO:-konkanello/trail_ptw_dumps}"
while [ $# -gt 0 ]; do case "$1" in
  --analyse|--analyze) ACTION="analyse"; shift;;
  --plot) ACTION="plot"; shift;;
  --download) ACTION="download"; shift;;
  --allow-partial) PARTIAL=1; shift;;
  --dumps) DUMPS="$2"; shift 2;;
  --hf-repo) HF_REPO="$2"; shift 2;;
  --mode) MODE="$2"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --partitions) PARTS="$2"; shift 2;;
  --out) OUT="$2"; OUT_SET=1; shift 2;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done

# --- step 0: download --------------------------------------------------------
# Runs through the harness so `hf` resolves from the AE venv (lib/venv.sh, sourced
# above). A user never has to activate anything. Downloads land in a directory the
# analysis step already searches, so step 1 then needs no --dumps.
if [ "$ACTION" = "download" ]; then
  DL="${DUMPS:-$ROOT/ptw_bundle}"
  if ! command -v hf >/dev/null 2>&1; then
    echo "ERROR: the 'hf' CLI is not available." >&2
    echo "  It lives in the AE virtualenv. Create it with:" >&2
    echo "      bash experiments/ae/lib/install_deps.sh" >&2
    echo "  then re-run this command (no need to activate anything)." >&2
    exit 1
  fi
  echo "==== downloading the PTW dumps (~2.9 GB) ===="
  echo "  dataset: $HF_REPO"
  echo "  into:    $DL"
  echo "  (resumable — if it is interrupted, just run this command again)"
  hf download "$HF_REPO" --repo-type dataset --local-dir "$DL" \
    || { echo "ERROR: download failed. Re-run to resume; if the dataset is private," >&2
         echo "       authenticate first with:  $(command -v hf) auth login" >&2; exit 1; }
  n=$(ls "$DL"/ptw_dumps/*.csv "$DL"/ptw_dumps/*.csv.gz "$DL"/*.csv "$DL"/*.csv.gz 2>/dev/null | wc -l)
  echo "  downloaded $n dumps."
  echo
  echo "==== next: analyse (step 1 of 2) ===="
  case "$DL" in
    "$ROOT/ptw_bundle"|"$ROOT/../ptw_bundle"|"${HOME:-/nonexistent}/ptw_bundle")
      echo "  bash experiments/ae/motivation/run_motivation.sh --mode local --jobs \$(nproc)";;
    *)  # not one of the searched locations — the path has to be passed explicitly
      echo "  bash experiments/ae/motivation/run_motivation.sh --dumps $DL --mode local --jobs \$(nproc)";;
  esac
  exit 0
fi
# --- locate the dumps --------------------------------------------------------
# A pre-staged bundle wins over downloading 2.9 GB again. Same convention as the
# trace bundle in build_and_validate.sh: it normally sits one level ABOVE the
# artifact root, and on a shared machine it is usually a symlink to a common copy.
# true if <dir> (or its ptw_dumps/ subdir) holds at least one dump. Tested file
# by file: `ls a/*.csv b/*.csv` exits non-zero as soon as ONE glob is unmatched,
# which is the normal case here (a dataset root has only ptw_dumps/).
has_dumps() {
  local d f
  for d in "$1" "$1/ptw_dumps"; do
    [ -d "$d" ] || continue
    for f in "$d"/*.csv "$d"/*.csv.gz; do [ -e "$f" ] && return 0; done
  done
  return 1
}
if [ -z "$DUMPS" ]; then
  for cand in "${HOME:-/nonexistent}/ptw_bundle" "$ROOT/../ptw_bundle" "$ROOT/ptw_bundle"; do
    [ -d "$cand" ] || continue
    has_dumps "$cand" || continue
    DUMPS="$cand"
    echo "  found a pre-staged PTW-dump bundle: $cand"
    [ -L "$cand" ] && echo "  (symlink -> $(readlink -f "$cand"))"
    echo "  (the download is not needed; override with --dumps <dir>)"
    break
  done
fi
[ -n "$DUMPS" ] || { cat >&2 <<'NODUMPS'
ERROR: no PTW dumps found, and --dumps <dir> was not given.
Download them (~2.9 GB, resumable):
    bash experiments/ae/motivation/run_motivation.sh --download
or point at an existing bundle:
    run_motivation.sh --dumps <dir>          # dataset root or its ptw_dumps/ subdir
or pre-stage one where this script looks for it ($HOME/ptw_bundle,
<artifact>/../ptw_bundle, <artifact>/ptw_bundle) — on a shared machine that is
normally a symlink, e.g.  ln -s /path/to/shared/ptw_bundle ~/ptw_bundle
NODUMPS
exit 2; }
[ -d "$DUMPS" ] || { echo "ERROR: --dumps $DUMPS is not a directory"; exit 2; }
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
have() { ls "$JDIR"/*.json 2>/dev/null | wc -l; }
plot_cmd="bash experiments/ae/motivation/run_motivation.sh --plot${OUT_SET:+ --out $OUT}"

# --- step 2: plot ------------------------------------------------------------
# Separate from the analysis on purpose: in SLURM mode the analysis only submits
# jobs, so the figures cannot exist until they finish. Plotting is its own step
# in BOTH modes so the same two commands work everywhere.
if [ "$ACTION" = "plot" ]; then
  rm -f "$work"
  n=$(have)
  echo "==== plotting motivation figures -> $OUT ===="
  echo "  analysed workloads: $n/$total"
  if [ "$n" -eq 0 ]; then
    echo "ERROR: no analysis results in $JDIR — run the analysis step first:" >&2
    echo "    bash experiments/ae/motivation/run_motivation.sh --mode <local|slurm>" >&2
    exit 1
  fi
  if [ "$n" -lt "$total" ] && [ "$PARTIAL" -eq 0 ]; then
    echo "ERROR: only $n of $total workloads analysed — the figures would be incomplete." >&2
    echo "  still running?   squeue -u \$USER | grep -c mot_      (SLURM)" >&2
    echo "  wait and re-run this command, or pass --allow-partial to plot anyway." >&2
    exit 1
  fi
  python3 "$HERE/plot_motivation.py" --json-dir "$JDIR" --dumps-dir "$DUMPS" --out-dir "$OUT" \
    && echo "figures in $OUT" || { echo "  (plot failed — need matplotlib)"; exit 1; }
  exit 0
fi

# --- step 1: analyse ---------------------------------------------------------
echo "==== motivation analysis: $todo of $total workloads to run (mode=$MODE) ===="
if [ "$todo" -gt 0 ]; then
  if [ "$MODE" = "slurm" ]; then
    while IFS= read -r dump; do
      wl=$(basename "$dump"); wl="${wl%.csv.gz}"; wl="${wl%.csv}"
      sbatch ${PARTS:+--partition=$PARTS} -c 2 -J "mot_$wl" \
        --output="$JDIR/$wl.slurm.out" --error="$JDIR/$wl.slurm.err" \
        --wrap="python3 '$AN' --dump '$dump' --workload '$wl' --out '$JDIR/$wl.json'" >/dev/null
    done < "$work"
    echo "  submitted $todo SLURM jobs."
  else
    echo "  running locally on up to $JOBS cores ..."
    xargs -a "$work" -P "$JOBS" -I{} bash -c '
      d="{}"; wl=$(basename "$d"); wl="${wl%.csv.gz}"; wl="${wl%.csv}"
      python3 "'"$AN"'" --dump "$d" --workload "$wl" --out "'"$JDIR"'/$wl.json" >/dev/null 2>&1'
  fi
fi
rm -f "$work"

n=$(have)
echo "  analysed: $n/$total workloads."
echo
if [ "$n" -lt "$total" ]; then
  echo "==== next: wait for the analysis, then plot (step 2 of 2) ===="
  echo "  progress:  ls $JDIR/*.json | wc -l    # should reach $total"
  [ "$MODE" = "slurm" ] && echo "  queue:     squeue -u \$USER | grep -c mot_"
else
  echo "==== next: produce the figures (step 2 of 2) ===="
fi
echo "  plot:      $plot_cmd"
