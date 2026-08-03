#!/bin/bash
# ae_common.sh — shared suite registry + jobfile generation for the AE phase
# scripts (ae_launch.sh / ae_watch.sh / ae_results.sh). Sourced, not executed.

# Resolve artifact paths relative to this lib (…/experiments/ae/lib).
AE_LIB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_DIR="$(cd "$AE_LIB/.." && pwd)"            # experiments/ae
EXP="$(cd "$AE_DIR/.." && pwd)"               # experiments
ROOT="$(cd "$EXP/.." && pwd)"                 # artifact root
source "$AE_LIB/venv.sh"                       # put the AE Python venv on PATH (python3, hf)
YAML_SC="$EXP/clist_revelator.yaml"
YAML_MC="$EXP/clist_multicore.yaml"
AE_OUT="$AE_DIR/ae_out"

ALL_SUITES="revelator revelator_thp utilsweep multicore"

# suite -> (generator, suite(s), exp-dir, parser, parser-args, paper label, figure file)
ae_suite_cfg() {  # sets GEN EXPSUITE DIR PARSER PARGS FIG FIGFILE ; returns 1 on unknown suite
  case "$1" in
    revelator)     GEN=sc; EXPSUITE="revelator_headtohead";     DIR="ae_revelator";     PARSER=parse_revelator.py; PARGS="--variant base"; FIG="Revelator (4KB) head-to-head";      FIGFILE="revelator";;
    revelator_thp) GEN=sc; EXPSUITE="revelator_thp_headtohead"; DIR="ae_revelator_thp"; PARSER=parse_revelator.py; PARGS="--variant thp";  FIG="Revelator-THP head-to-head";        FIGFILE="revelator_thp";;
    utilsweep)     GEN=sc; EXPSUITE="revelator_util_sweep";     DIR="ae_utilsweep";     PARSER=parse_utilsweep.py; PARGS="";               FIG="Memory-utilization sensitivity";    FIGFILE="utilsweep";;
    multicore)     GEN=mc; EXPSUITE="ae_revelator_4core";       DIR="ae_multicore";     PARSER=parse_multicore.py; PARGS="";               FIG="4-core Revelator head-to-head";     FIGFILE="multicore";;
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
