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

# DRAM speed (MT/s) sweep on 2 MB NUCA, top-200 workloads.  Bus bandwidth
# scales with dram_speed in the DDR model; row timings (15 ns) stay fixed in ns
# as in real DDR speed grades, and the column-to-column delays (tCCD, defined in
# clock cycles) are scaled by 2400/MT/s from the 2400 MT/s defaults, so the
# 2400 MT/s point is the head2mb suite itself.
MTPS_VALUES="400 800 1600 3200 4800"
MTPS_SUITES="$(for m in $MTPS_VALUES; do printf 'head2mb_mtps%s ' $m; done)"
MTPS_CONFIGS="v4-noprefetch-nuca2mb v4-asp-recency-nuca2mb v4-trail-nuca2mb"
ALL_SUITES="head8mb head2mb table5 table6 multicore abl2mb ${MTPS_SUITES}motivation"

# suite -> (generator, suite(s), exp-dir, parser, parser-args, paper label, figure file)
# KIND=sim for the simulation suites, KIND=mot for the motivation analysis, which
# is driven by motivation/run_motivation.sh instead of a generated jobfile. The
# phase scripts dispatch on KIND; everything else about a suite — the ae_out
# status/DONE contract, and therefore ae_run_all's three passes — is identical.
ae_suite_cfg() {  # sets KIND GEN EXPSUITE DIR FIG FIGFILE ; returns 1 on unknown suite
  KIND=sim; MTPS=""; TOP200_ONLY=""
  case "$1" in
    head2mb_mtps*)
      case " $MTPS_SUITES " in *" $1 "*) ;; *) echo "unknown suite: $1" >&2; return 1;; esac
      ae_suite_cfg head2mb || return 1
      MTPS="${1#head2mb_mtps}"; DIR="${DIR}_mtps${MTPS}"; FIG="Figure 18 (DRAM ${MTPS} MT/s)"; FIGFILE="figure18"
      EXPDIR="$EXP/exp_${DIR}"; RESULTS="$EXPDIR/results"; JOBFILE="$EXPDIR/jobfile.sh"
      return 0;;
    head8mb)   GEN=sc; EXPSUITE="trail_comparison_v4";             DIR="ae_head8mb"; FIG="Figure 11 (bottom, 8 MB LLC)"; FIGFILE="figure11";;
    head2mb)   GEN=sc; EXPSUITE="trail_comparison_v4_nuca2mb";     DIR="ae_head2mb"; FIG="Figure 11 (top, 2 MB LLC), Figure 13"; FIGFILE="figure11";;
    table5)    GEN=sc; EXPSUITE="trail-pte-budget-grid-corrected"; DIR="ae_table5";  FIG="Table 4";  FIGFILE="table4";;
    table6)    GEN=sc; EXPSUITE="sidecar-payload-sweep-corrected"; DIR="ae_table6";  FIG="Table 5";  FIGFILE="table5";;
    # TRAIL component ablation; full TRAIL is v4-trail-nuca2mb in head2mb
    abl2mb)    GEN=sc; EXPSUITE="trail_ablation_v4_nuca2mb"; DIR="ae_abl2mb"; FIG="Figure 19"; FIGFILE="figure19"; TOP200_ONLY=1;;
    multicore) GEN=mc; EXPSUITE="prefetcher_v4_diverse_4core prefetcher_v4_diverse_4core_x60"; DIR="ae_multicore"; FIG="Figure 16"; FIGFILE="figure16";;
    motivation) KIND=mot; GEN=mot; EXPSUITE=""; DIR="motivation"; FIG="Figures 2, 3, 5, 6, 7"; FIGFILE="figure2_topk_coverage";;
    *) echo "unknown suite: $1 (valid: $ALL_SUITES)" >&2; return 1;;
  esac
  if [ "$KIND" = "mot" ]; then
    # no jobfile: one analysis per PTW dump, one JSON per workload
    EXPDIR="$AE_DIR/motivation"; RESULTS="$EXPDIR/motivation_out/json"; JOBFILE=""
  else
    EXPDIR="$EXP/exp_${DIR}"; RESULTS="$EXPDIR/results"; JOBFILE="$EXPDIR/jobfile.sh"
  fi
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
  [ -n "$MTPS" ] && ae_mtps_postprocess "$1"
  [ -n "$TOP200_ONLY" ] && ae_top200_filter
  return 0
}


# expected run-dir basenames from a jobfile (one per job)
ae_expected_rundirs() { grep -oE -- '-d [^ ]+/results/[^ ]+' "$1" 2>/dev/null | awk '{print $2}'; }



# head2mb_mtps<N>: keep MTPS_CONFIGS on the top-200 workloads, append the DRAM
# speed and scaled tCCD overrides at the end of each command.  Idempotent.
ae_mtps_postprocess() {  # $1 = head2mb_mtps<N> (ae_suite_cfg already applied)
  python3 - "$JOBFILE" "$RESULTS" "$MTPS" "$MTPS_CONFIGS" "$TOP200" <<'PYEOF' || return 1
import os, re, sys
jf, results, mtps, cfgs, top200 = sys.argv[1:]
mtps = int(mtps); cfgs = cfgs.split()
top = {l.strip() for l in open(top200) if l.strip() and not l.startswith('#')}
scale = 2400.0 / mtps
def ns(x): return ('%.4f' % (x * scale)).rstrip('0').rstrip('.')
args = ('-g --perf_model/dram/ddr/dram_speed=%d'
        ' -g --perf_model/dram/ddr/intercommand_delay=%s'
        ' -g --perf_model/dram/ddr/intercommand_delay_short=%s'
        ' -g --perf_model/dram/ddr/intercommand_delay_long=%s') % (mtps, ns(5.0), ns(2.5), ns(5.0))
out, kept, dropped = [], 0, 0
for line in open(jf):
    if not line.startswith('sbatch'):
        out.append(line); continue
    d = re.search(r' -d (\S+)', line).group(1); rd = os.path.basename(d)
    c = next((c for c in sorted(cfgs, key=len, reverse=True) if rd.startswith(c + '_')), None)
    if c is None or rd[len(c) + 1:] not in top:
        if os.path.isdir(d) and not os.path.islink(d) and not os.listdir(d): os.rmdir(d)
        dropped += 1; continue
    for key in re.findall(r'--([^= ]+)=', args):
        assert key not in line, 'experiment already sets ' + key + ': ' + line[:200]
    q = line.rindex('"')
    line = line[:q].rstrip() + ' ' + args + ' ' + line[q:]
    line = re.sub(r' -J (\S+)', r' -J mtps%d_\1' % mtps, line, count=1)
    out.append(line); kept += 1
open(jf, 'w').writelines(out)
print(f'  [mtps{mtps}] {kept} jobs ({args}); {dropped} dropped', file=sys.stderr)
PYEOF
}

# keep only jobs whose workload is on the top-200 list (run-dir = <config>_<workload>)
ae_top200_filter() {
  python3 - "$JOBFILE" "$TOP200" <<'PYEOF' || return 1
import os, re, sys
jf, top200 = sys.argv[1:]
top = {l.strip() for l in open(top200) if l.strip() and not l.startswith('#')}
out, kept, dropped = [], 0, 0
for line in open(jf):
    if not line.startswith('sbatch'): out.append(line); continue
    d = re.search(r' -d (\S+)', line).group(1); rd = os.path.basename(d)
    if any(rd.endswith('_' + w) for w in top): out.append(line); kept += 1
    else:
        if os.path.isdir(d) and not os.path.islink(d) and not os.listdir(d): os.rmdir(d)
        dropped += 1
open(jf, 'w').writelines(out)
print(f'  [top200] kept {kept} jobs, dropped {dropped}', file=sys.stderr)
PYEOF
}
