#!/bin/bash
# build_and_validate.sh ARTIFACT_ROOT
# Builds lib/sniper from source, then runs a post-build smoke test (one short
# single-core simulation) and asserts a valid IPC is produced. Non-zero exit
# if the build fails OR the smoke sim does not produce a parseable result.
set -euo pipefail
ROOT="${1:?usage: build_and_validate.sh ARTIFACT_ROOT}"
SNIPER="$ROOT/simulator/sniper"

echo "==================================================================="
echo " [1/2] Building Sniper (make -j) ..."
echo "==================================================================="
cd "$SNIPER"
make -j"$(nproc)"
test -x "$SNIPER/lib/sniper" || { echo "BUILD FAILED: lib/sniper missing"; exit 1; }
echo "lib/sniper built: $(ls -la lib/sniper | awk '{print $5" bytes, "$6" "$7" "$8}')"

echo "==================================================================="
echo " [2/2] Post-build smoke test (10M-instr single-core sim) ..."
echo "==================================================================="
# Pick a smoke trace: prefer an env override, else the first champsim trace in
# the head-to-head trace list.
SMOKE_TRACE="${AE_SMOKE_TRACE:-}"
if [ -z "$SMOKE_TRACE" ]; then
  TL="$ROOT/experiments/exp_table5_pte_budget_corrected/trace_list.csv"
  [ -f "$TL" ] && SMOKE_TRACE=$(awk -F, 'NR==2{print $NF}' "$TL" 2>/dev/null || true)
fi
if [ -z "$SMOKE_TRACE" ] || [ ! -e "$SMOKE_TRACE" ]; then
  echo "WARNING: no smoke trace found (set AE_SMOKE_TRACE=/path/to/trace); skipping sim validation." >&2
  echo "Build OK; simulation NOT validated."
  exit 0
fi
OUT="$SNIPER/../../experiments/ae/_smoke_out"
rm -rf "$OUT"
echo "smoke trace: $SMOKE_TRACE"
"$SNIPER/run-sniper" --no-cache-warming --genstats -s stop-by-icount:10000000 \
  -c "$SNIPER/config/address_translation_schemes/trail_comparison_v4/v4_asp.cfg" \
  -g --perf_model/reserve_thp/target_fragmentation=0.0 \
  -d "$OUT" --traces="$SMOKE_TRACE" > "$OUT.log" 2>&1 || {
    echo "SMOKE SIM FAILED (see $OUT.log)"; tail -20 "$OUT.log"; exit 1; }

STATS="$OUT/simulation/sim.stats"
IC=$(grep -E '^performance_model.instruction_count =' "$STATS" 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' ')
CY=$(grep -E '^performance_model.cycle_count =' "$STATS" 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' ')
if [ -z "${IC:-}" ] || [ -z "${CY:-}" ]; then
  echo "SMOKE VALIDATION FAILED: no IPC counters in $STATS"; exit 1; fi
IPC=$(awk -v i="$IC" -v c="$CY" 'BEGIN{ if(c>0) printf "%.4f", i/c; else print "NaN"}')
echo "smoke IPC = $IPC  (instr=$IC, cycles=$CY)"
awk -v ipc="$IPC" 'BEGIN{ exit !(ipc+0 > 0) }' \
  && echo "BUILD + SIMULATION VALIDATED OK." \
  || { echo "SMOKE VALIDATION FAILED: non-positive IPC"; exit 1; }
