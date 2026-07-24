#!/bin/bash
# ae_common.sh — shared claim registry + jobfile generation for the AE phase
# scripts (ae_launch.sh / ae_watch.sh / ae_results.sh). Sourced, not executed.

# Resolve artifact paths relative to this lib (…/experiments/ae/lib).
AE_LIB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_DIR="$(cd "$AE_LIB/.." && pwd)"            # experiments/ae
EXP="$(cd "$AE_DIR/.." && pwd)"               # experiments
ROOT="$(cd "$EXP/.." && pwd)"                 # artifact root
YAML_SC="$EXP/clist_prefetcher_v3.yaml"
YAML_MC="$EXP/clist_multicore.yaml"
TOP200="$EXP/top200_trail_workloads.txt"
AE_OUT="$AE_DIR/ae_out"

ALL_CLAIMS="head8mb head2mb table5 table6 pqsweep multicore"

# claim -> (generator, suite(s), exp-dir, parser, parser-args, paper label, figure file)
ae_claim_cfg() {  # sets GEN SUITE DIR PARSER PARGS FIG FIGFILE ; returns 1 on unknown claim
  case "$1" in
    head8mb)   GEN=sc; SUITE="trail_comparison_v4";             DIR="ae_head8mb"; PARSER=parse_headtohead.py; PARGS="";                 FIG="Figure 12 (bottom, 8 MB NUCA)"; FIGFILE="figure12";;
    head2mb)   GEN=sc; SUITE="trail_comparison_v4_nuca2mb";     DIR="ae_head2mb"; PARSER=parse_headtohead.py; PARGS="--suffix=-nuca2mb"; FIG="Figure 12 (top, 2 MB NUCA)";     FIGFILE="figure12";;
    table5)    GEN=sc; SUITE="trail-pte-budget-grid-corrected"; DIR="ae_table5";  PARSER=parse_table5.py;    PARGS="";                 FIG="Table 5";  FIGFILE="table5";;
    table6)    GEN=sc; SUITE="sidecar-payload-sweep-corrected"; DIR="ae_table6";  PARSER=parse_table6.py;    PARGS="";                 FIG="Table 6";  FIGFILE="table6";;
    pqsweep)   GEN=sc; SUITE="pq-size-sweep";                   DIR="ae_pqsweep"; PARSER=parse_pqsweep.py;   PARGS="";                 FIG="Figure 20"; FIGFILE="figure20";;
    multicore) GEN=mc; SUITE="prefetcher_v4_diverse_4core prefetcher_v4_diverse_4core_x60"; DIR="ae_multicore"; PARSER=parse_multicore.py; PARGS=""; FIG="Figure 22"; FIGFILE="figure22";;
    *) echo "unknown claim: $1 (valid: $ALL_CLAIMS)" >&2; return 1;;
  esac
  EXPDIR="$EXP/exp_${DIR}"; RESULTS="$EXPDIR/results"; JOBFILE="$EXPDIR/jobfile.sh"
}

# generate the jobfile for a claim (idempotent; --force overwrites)
ae_generate() {  # $1=claim
  ae_claim_cfg "$1" || return 1
  if [ "$GEN" = "sc" ]; then
    python3 "$EXP/create_experiments.py" --artifact-path "$ROOT" --yaml "$YAML_SC" \
      --suite $SUITE --suite-dir-name "$DIR" --force ${AE_EXCLUDE:+--exclude "$AE_EXCLUDE"} >/dev/null
  else
    python3 "$EXP/create_multicore_experiments.py" --artifact-path "$ROOT" --yaml "$YAML_MC" \
      --suite $SUITE --suite-dir-name "$DIR" --force ${AE_EXCLUDE:+--exclude "$AE_EXCLUDE"} >/dev/null
  fi
}

# expected run-dir basenames from a jobfile (one per job)
ae_expected_rundirs() { grep -oE -- '-d [^ ]+/results/[^ ]+' "$1" 2>/dev/null | awk '{print $2}'; }
