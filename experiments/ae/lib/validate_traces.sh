#!/bin/bash
# validate_traces.sh — post-setup smoke test.
#
# After you download the traces and run setup_tlists.sh, this runs a few short
# single-core simulations on RANDOMLY chosen traces to confirm that the traces,
# the resolved trace-lists and the simulator all work end-to-end — before you
# launch the full (thousands-of-jobs) experiment.
#
#   experiments/ae/lib/validate_traces.sh [--n N] [--icount M]
#                                         [--tlist-dir DIR] [--config CFG]
#
# Exits 0 only if every chosen trace produced a valid IPC.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"          # artifact root
SNIPER="$ROOT/simulator/sniper"

N=3
ICOUNT=1000000
TLDIR="$ROOT/experiments/vm_tlist"
CFG="$SNIPER/config/address_translation_schemes/trail_comparison_v4/v4_asp.cfg"
while [ $# -gt 0 ]; do case "$1" in
  --n) N="$2"; shift 2;;
  --icount) ICOUNT="$2"; shift 2;;
  --tlist-dir) TLDIR="$2"; shift 2;;
  --config) CFG="$2"; shift 2;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done

[ -x "$SNIPER/lib/sniper" ] || { echo "ERROR: $SNIPER/lib/sniper not built (run --build first)."; exit 1; }
[ -f "$CFG" ] || { echo "ERROR: config not found: $CFG"; exit 1; }
[ -d "$TLDIR" ] || { echo "ERROR: trace-list dir not found: $TLDIR (run setup_tlists.sh first)."; exit 1; }

# --- collect every single-core trace path from the resolved trace-lists, keep
#     only those that actually exist on disk (i.e. were downloaded) ------------
mapfile -t EXISTING < <(
  for t in "$TLDIR"/top250_*.tlist; do
    [ -f "$t" ] || continue
    root=$(sed -n '1p' "$t")
    grep -vE '^#|^[[:space:]]*$' "$t" | tail -n +2 | while IFS=, read -r _name file _rest; do
      file="${file//[[:space:]]/}"
      [ -n "$file" ] && [ -f "$root/$file" ] && echo "$root/$file"
    done
  done | sort -u
)
navail=${#EXISTING[@]}
if [ "$navail" -eq 0 ]; then
  echo "ERROR: no existing traces found via $TLDIR/top250_*.tlist."
  echo "       Did you download the traces and run setup_tlists.sh with the right traces dir?"
  exit 1
fi
[ "$N" -gt "$navail" ] && N="$navail"
echo "=== AE trace validation: $N random trace(s) of $navail available, ${ICOUNT} instr each ==="

# --- pick N at random and simulate each -------------------------------------
mapfile -t PICKED < <(printf '%s\n' "${EXISTING[@]}" | shuf | head -n "$N")
pass=0
for tr in "${PICKED[@]}"; do
  out=$(mktemp -d)
  "$SNIPER/run-sniper" --no-cache-warming --genstats -s "stop-by-icount:${ICOUNT}" \
     -c "$CFG" -g --perf_model/reserve_thp/target_fragmentation=0.0 \
     -d "$out" --traces="$tr" > "$out/run.log" 2>&1
  s="$out/simulation/sim.stats"
  ic=$(grep -m1 '^performance_model.instruction_count =' "$s" 2>/dev/null | cut -d= -f2 | tr -d ' ')
  cy=$(grep -m1 '^performance_model.cycle_count =' "$s" 2>/dev/null | cut -d= -f2 | tr -d ' ')
  if [ -n "${ic:-}" ] && [ -n "${cy:-}" ] && [ "${cy%.*}" -gt 0 ] 2>/dev/null; then
    ipc=$(awk -v i="$ic" -v c="$cy" 'BEGIN{printf "%.4f", i/c}')
    printf "  [PASS] %-40s IPC=%s (instr=%s)\n" "$(basename "$tr")" "$ipc" "$ic"
    pass=$((pass+1)); rm -rf "$out"
  else
    printf "  [FAIL] %-40s no valid sim.stats — see %s/run.log\n" "$(basename "$tr")" "$out"
  fi
done

echo "=== $pass/${#PICKED[@]} traces produced a valid IPC ==="
[ "$pass" -eq "${#PICKED[@]}" ] && [ "$pass" -gt 0 ] && { echo "Setup validated. You can now launch the full run."; exit 0; }
echo "Validation FAILED — fix the trace/tlist setup before launching the full run." >&2
exit 1
