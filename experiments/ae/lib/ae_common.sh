#!/bin/bash
# ae_common.sh — shared suite registry + jobfile generation for the AE phase
# scripts (ae_launch.sh / ae_watch.sh / ae_results.sh). Sourced, not executed.

# Resolve artifact paths relative to this lib (…/experiments/ae/lib).
AE_LIB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_DIR="$(cd "$AE_LIB/.." && pwd)"            # experiments/ae
EXP="$(cd "$AE_DIR/.." && pwd)"               # experiments
ROOT="$(cd "$EXP/.." && pwd)"                 # artifact root
source "$AE_LIB/venv.sh"                       # put the AE Python venv on PATH (python3, hf)
YAML_SC="$EXP/clist_prefetcher_v3.yaml"
YAML_MC="$EXP/clist_multicore.yaml"
TOP200="$EXP/top200_trail_workloads.txt"
AE_OUT="$AE_DIR/ae_out"

ALL_SUITES="head8mb head2mb table5 table6 pqsweep multicore"

# suite -> (generator, suite(s), exp-dir, parser, parser-args, paper label, figure file)
ae_suite_cfg() {  # sets GEN EXPSUITE DIR PARSER PARGS FIG FIGFILE ; returns 1 on unknown suite
  case "$1" in
    head8mb)   GEN=sc; EXPSUITE="trail_comparison_v4";             DIR="ae_head8mb"; PARSER=parse_headtohead.py; PARGS="";                 FIG="Figure 12 (bottom, 8 MB NUCA)"; FIGFILE="figure12";;
    head2mb)   GEN=sc; EXPSUITE="trail_comparison_v4_nuca2mb";     DIR="ae_head2mb"; PARSER=parse_headtohead.py; PARGS="--suffix=-nuca2mb"; FIG="Figure 12 (top, 2 MB NUCA)";     FIGFILE="figure12";;
    table5)    GEN=sc; EXPSUITE="trail-pte-budget-grid-corrected"; DIR="ae_table5";  PARSER=parse_table5.py;    PARGS="";                 FIG="Table 5";  FIGFILE="table5";;
    table6)    GEN=sc; EXPSUITE="sidecar-payload-sweep-corrected"; DIR="ae_table6";  PARSER=parse_table6.py;    PARGS="";                 FIG="Table 6";  FIGFILE="table6";;
    pqsweep)   GEN=sc; EXPSUITE="pq-size-sweep";                   DIR="ae_pqsweep"; PARSER=parse_pqsweep.py;   PARGS="";                 FIG="Figure 20"; FIGFILE="figure20";;
    multicore) GEN=mc; EXPSUITE="prefetcher_v4_diverse_4core prefetcher_v4_diverse_4core_x60"; DIR="ae_multicore"; PARSER=parse_multicore.py; PARGS=""; FIG="Figure 22"; FIGFILE="figure22";;
    *) echo "unknown suite: $1 (valid: $ALL_SUITES)" >&2; return 1;;
  esac
  EXPDIR="$EXP/exp_${DIR}"; RESULTS="$EXPDIR/results"; JOBFILE="$EXPDIR/jobfile.sh"
}

# generate the jobfile for a suite (idempotent; --force overwrites)
ae_generate() {  # $1=suite
  ae_suite_cfg "$1" || return 1
  if [ "$GEN" = "sc" ]; then
    python3 "$EXP/create_experiments.py" --artifact-path "$ROOT" --yaml "$YAML_SC" \
      --suite $EXPSUITE --suite-dir-name "$DIR" --force ${AE_EXCLUDE:+--exclude "$AE_EXCLUDE"} >/dev/null
  else
    python3 "$EXP/create_multicore_experiments.py" --artifact-path "$ROOT" --yaml "$YAML_MC" \
      --suite $EXPSUITE --suite-dir-name "$DIR" --force ${AE_EXCLUDE:+--exclude "$AE_EXCLUDE"} >/dev/null
  fi
}

# expected run-dir basenames from a jobfile (one per job)
ae_expected_rundirs() { grep -oE -- '-d [^ ]+/results/[^ ]+' "$1" 2>/dev/null | awk '{print $2}'; }
