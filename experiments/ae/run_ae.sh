#!/bin/bash
# ===========================================================================
# Artifact-Evaluation driver for the TRAIL TLB-prefetcher study.
#
#   run_ae.sh --mode {slurm|local} --claim {head8mb,head2mb,table5,table6,
#                                           pqsweep,multicore,all}
#             [--jobs N] [--build] [--install-deps] [--out DIR]
#             [--partitions p1,p2] [--no-preflight]   (partitions optional;
#                    omit to use the cluster default — nothing is passed to sbatch)
#             [--max-retries N] [--dry-run] [--artifact-root DIR]
#
# Per claim: generate jobfile -> PRE-FLIGHT traces -> launch+heal (resumable,
# auto-resubmits transient failures) -> parse into a markdown table under --out.
# Completion is VALIDITY-based: a job counts as done only when its sim.stats
# carries the IPC counters. --build compiles lib/sniper + runs a smoke sim.
# ===========================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
EXP="$(cd "$HERE/.." && pwd)"                 # experiments/
ROOT="$(cd "$EXP/.." && pwd)"                 # artifact root
YAML_SC="$EXP/clist_prefetcher_v3.yaml"
YAML_MC="$EXP/clist_multicore.yaml"
TOP200="$EXP/top200_trail_workloads.txt"
source "$HERE/lib/jobtools.sh"

MODE=""; CLAIMS=""; JOBS=$(( $(nproc) - 2 )); DO_BUILD=0; DO_INSTALL=0
OUT="$EXP/ae/ae_out"; DRY=0; PARTS=""; NO_PREFLIGHT=0; MAX_RETRIES=3; EXCLUDE=""
while [ $# -gt 0 ]; do case "$1" in
  --mode) MODE="$2"; shift 2;;
  --claim) CLAIMS="$2"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --build) DO_BUILD=1; shift;;
  --install-deps) DO_INSTALL=1; shift;;
  --out) OUT="$2"; shift 2;;
  --partitions) PARTS="$2"; shift 2;;
  --exclude) EXCLUDE="$2"; shift 2;;
  --no-preflight) NO_PREFLIGHT=1; shift;;
  --max-retries) MAX_RETRIES="$2"; shift 2;;
  --artifact-root) ROOT="$2"; shift 2;;
  --dry-run) DRY=1; shift;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
[ -z "$MODE" ] && { echo "ERROR: --mode {slurm|local} required"; exit 2; }
[ -z "$CLAIMS" ] && { echo "ERROR: --claim required"; exit 2; }
[ "$CLAIMS" = "all" ] && CLAIMS="head8mb head2mb table5 table6 pqsweep multicore"
CLAIMS="${CLAIMS//,/ }"
[ "$JOBS" -lt 1 ] 2>/dev/null && JOBS=1
mkdir -p "$OUT"

# ---- claim registry -------------------------------------------------------
claim_cfg() {
  case "$1" in
    head8mb)   GEN=sc; SUITE="trail_comparison_v4";                 DIR="ae_head8mb";;
    head2mb)   GEN=sc; SUITE="trail_comparison_v4_nuca2mb";         DIR="ae_head2mb";;
    table5)    GEN=sc; SUITE="trail-pte-budget-grid-corrected";     DIR="ae_table5";;
    table6)    GEN=sc; SUITE="sidecar-payload-sweep-corrected";     DIR="ae_table6";;
    pqsweep)   GEN=sc; SUITE="pq-size-sweep";                       DIR="ae_pqsweep";;
    multicore) GEN=mc; SUITE="prefetcher_v4_diverse_4core prefetcher_v4_diverse_4core_x60"; DIR="ae_multicore";;
    *) echo "unknown claim: $1"; return 1;;
  esac
}

# ---- resumable launch + self-healing wait (validity-based) ----------------
run_and_heal() {  # $1=claim $2=expdir $3=expected
  local claim="$1" expdir="$2" exp="$3"
  local jf="$expdir/jobfile.sh" rd="$expdir/results" round=0 tmp nmiss valid
  while :; do
    tmp=$(mktemp)
    ae_missing_lines "$jf" "$PARTS" > "$tmp"           # NUL-sep sbatch lines lacking a valid result
    nmiss=$(tr -cd '\0' < "$tmp" | wc -c); valid=$(( exp - nmiss ))
    if [ "$nmiss" -eq 0 ]; then echo "[heal] $claim: all $exp valid."; rm -f "$tmp"; break; fi
    round=$((round+1))
    if [ "$round" -gt $((MAX_RETRIES+1)) ]; then
      echo "[heal] $claim: gave up after $MAX_RETRIES retries — $nmiss still failing ($valid/$exp)."; rm -f "$tmp"; break; fi
    echo "[heal] $claim round $round: $valid/$exp valid; (re)submitting $nmiss ..."
    if [ "$MODE" = "slurm" ]; then
      while IFS= read -r -d '' line; do eval "$line" >/dev/null 2>&1; done < "$tmp"
      while :; do
        local active d
        active=$(squeue -u "$USER" -h -t PD,R,CG,CF,S 2>/dev/null | wc -l)
        d=$(ae_count_valid "$rd")
        printf "[wait] %-10s r%s  %s/%s stats  active=%s  @ %s\n" "$claim" "$round" "$d" "$exp" "$active" "$(date '+%H:%M:%S')"
        [ "$active" -eq 0 ] && break
        sleep 60
      done
    else  # local: strip sbatch wrapper -> run-sniper, run JOBS at a time (blocks)
      python3 - "$tmp" <<'PY' | xargs -0 -n1 -P "$JOBS" bash -c
import sys, re
for b in open(sys.argv[1], "rb").read().split(b"\0"):
    line = b.decode(errors="replace").strip()
    if not line: continue
    m = re.search(r'native_wrapper\.sh\s+"(.*)"\s*$', line)
    cmd = m.group(1) if m else (re.findall(r'"([^"]*)"', line) or [""])[-1]
    if cmd.strip(): sys.stdout.write(cmd.strip() + "\0")
PY
    fi
    rm -f "$tmp"
  done
  echo "[done] $claim per-config valid counts:"
  local names; names=$(find "$rd" -maxdepth 3 -name sim.stats -printf '%h\n' 2>/dev/null | sed -E 's#/simulation$##' | xargs -r -n1 basename)
  while IFS=, read -r cfg _; do
    [ "$cfg" = "config_name" ] && continue
    printf "         %-30s %s\n" "$cfg" "$(printf '%s\n' "$names" | grep -c "^${cfg}_")"
  done < "$expdir/config_list.csv"
}

# ---- optional install / build ---------------------------------------------
[ "$DO_INSTALL" -eq 1 ] && { bash "$HERE/lib/install_deps.sh" || { echo "DEP INSTALL FAILED."; exit 1; }; }
[ "$DO_BUILD"   -eq 1 ] && { bash "$HERE/lib/build_and_validate.sh" "$ROOT" || { echo "BUILD/VALIDATE FAILED."; exit 1; }; }

# ---- run each claim -------------------------------------------------------
for claim in $CLAIMS; do
  claim_cfg "$claim" || continue
  echo; echo "############################################################"
  echo "# CLAIM: $claim   (mode=$MODE, gen=$GEN, partitions=$PARTS)"
  echo "############################################################"
  EXPDIR="$EXP/exp_${DIR}"
  if [ "$GEN" = "sc" ]; then
    python3 "$EXP/create_experiments.py" --artifact-path "$ROOT" --yaml "$YAML_SC" \
      --suite $SUITE --suite-dir-name "$DIR" --force ${EXCLUDE:+--exclude "$EXCLUDE"} >/dev/null
  else
    python3 "$EXP/create_multicore_experiments.py" --artifact-path "$ROOT" --yaml "$YAML_MC" \
      --suite $SUITE --suite-dir-name "$DIR" --force ${EXCLUDE:+--exclude "$EXCLUDE"} >/dev/null
  fi
  EXPECTED=$(grep -c '^sbatch' "$EXPDIR/jobfile.sh")
  echo "[gen] exp_${DIR}: $EXPECTED jobs (suite: $SUITE)"
  if [ "$DRY" -eq 1 ]; then echo "[dry-run] would preflight + launch ($MODE) + parse -> $OUT/${claim}.md"; continue; fi

  # PRE-FLIGHT: fail fast if any trace is unreachable
  if [ "$NO_PREFLIGHT" -eq 0 ]; then
    ae_preflight_traces "$EXPDIR/jobfile.sh" || { echo "[abort] $claim: preflight failed."; continue; }
  fi

  # LAUNCH + HEAL (resumable, validity-based, auto-resubmit)
  run_and_heal "$claim" "$EXPDIR" "$EXPECTED"

  # PARSE -> markdown
  MD="$OUT/${claim}.md"; RD="$EXPDIR/results"; echo "[parse] -> $MD"
  case "$claim" in
    table5)    python3 "$HERE/parse/parse_table5.py"    --results-dir "$RD" --top200 "$TOP200" --md "$MD";;
    table6)    python3 "$HERE/parse/parse_table6.py"    --results-dir "$RD" --top200 "$TOP200" --md "$MD";;
    pqsweep)   python3 "$HERE/parse/parse_pqsweep.py"   --results-dir "$RD" --top200 "$TOP200" --md "$MD";;
    head8mb)   python3 "$HERE/parse/parse_headtohead.py" --results-dir "$RD" --top200 "$TOP200" --suffix ""         --md "$MD";;
    head2mb)   python3 "$HERE/parse/parse_headtohead.py" --results-dir "$RD" --top200 "$TOP200" --suffix "-nuca2mb" --md "$MD";;
    multicore) python3 "$HERE/parse/parse_multicore.py" --results-dir "$RD" --md "$MD";;
  esac
done
echo; echo "=== AE run complete. Tables in $OUT ==="
ls -la "$OUT"
