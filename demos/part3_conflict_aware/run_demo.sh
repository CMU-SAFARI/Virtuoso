#!/bin/bash
# run_demo.sh -- Part 3: Compare baseline ReserveTHP vs conflict-aware allocator
#
# Runs two Virtuoso simulations:
#   1. Baseline:        ReserveTHP (standard page-table allocation)
#   2. Conflict-Aware:  Same workload, with conflict-aware PT allocation
#
# Since the conflict-aware allocator is a workshop exercise that requires
# integration into the Sniper build, this script runs the baseline and
# shows what metrics to compare.  If the conflict-aware allocator has been
# integrated, it runs both.
#
# Usage:
#   cd demos/part3_conflict_aware
#   bash run_demo.sh [trace_path]

set -e

# ---- Resolve paths ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SNIPER_ROOT="${REPO_ROOT}/simulator/sniper"

if [ ! -x "${SNIPER_ROOT}/run-sniper" ]; then
    echo "ERROR: Cannot find run-sniper at ${SNIPER_ROOT}/run-sniper"
    echo "       Make sure Virtuoso is built."
    exit 1
fi

# ---- Trace selection ----
TRACE="${1:-}"
if [ -z "${TRACE}" ]; then
    if [ -n "${VIRTUOSO_TRACES}" ] && [ -d "${VIRTUOSO_TRACES}" ]; then
        TRACE="${VIRTUOSO_TRACES}/rnd.sift"
    else
        echo "ERROR: No trace specified and VIRTUOSO_TRACES is not set."
        echo "Usage: $0 [path/to/trace.sift]"
        exit 1
    fi
fi

if [ ! -f "${TRACE}" ] && [ ! -f "${TRACE}.sift" ]; then
    echo "ERROR: Trace not found: ${TRACE}"
    exit 1
fi

CONFIG="${SNIPER_ROOT}/config/address_translation_schemes/reservethp.cfg"
ICOUNT=2000000
RESULTS_BASELINE="${SCRIPT_DIR}/results_baseline"
RESULTS_CONFLICT="${SCRIPT_DIR}/results_conflict_aware"

echo "============================================================"
echo "  Part 3: Baseline vs Conflict-Aware Allocator"
echo "============================================================"
echo "  Sniper root : ${SNIPER_ROOT}"
echo "  Config      : ${CONFIG}"
echo "  Trace       : ${TRACE}"
echo "  Instructions: ${ICOUNT}"
echo ""

# ---- Clean previous results ----
rm -rf "${RESULTS_BASELINE}" "${RESULTS_CONFLICT}"

# ---- Helper: extract a stat ----
extract_stat() {
    local dir="$1"
    local pattern="$2"
    local stats_file="${dir}/sim.stats"
    if [ ! -f "${stats_file}" ]; then
        echo "N/A"
        return
    fi
    grep -m1 "${pattern}" "${stats_file}" 2>/dev/null | awk '{print $NF}' || echo "N/A"
}

extract_from_simout() {
    local dir="$1"
    local pattern="$2"
    local simout="${dir}/sim.out"
    if [ ! -f "${simout}" ]; then
        echo "N/A"
        return
    fi
    grep -m1 "${pattern}" "${simout}" 2>/dev/null | awk '{print $NF}' || echo "N/A"
}

# ---- Run baseline ----
echo "[1/2] Running BASELINE (ReserveTHP, frag=0.0) ..."
"${SNIPER_ROOT}/run-sniper" \
    -c "${CONFIG}" \
    -d "${RESULTS_BASELINE}" \
    -n 1 \
    -s "stop-by-icount:${ICOUNT}" \
    -g "perf_model/reserve_thp/target_fragmentation=0.0" \
    --traces="${TRACE}" \
    > "${RESULTS_BASELINE}.stdout.log" 2>&1 || {
        echo "ERROR: Baseline simulation failed. Check ${RESULTS_BASELINE}.stdout.log"
        exit 1
    }
echo "  Done. Results in ${RESULTS_BASELINE}"

# ---- Run conflict-aware (if integrated) ----
# Check if the conflict-aware allocator type is available by looking for
# it in the allocator factory.  If not yet integrated, we skip and show
# baseline-only results.
CONFLICT_AVAILABLE=false
if grep -q "conflict_aware" "${SNIPER_ROOT}/include/memory_management/physical_memory_allocators/"*.h 2>/dev/null; then
    CONFLICT_AVAILABLE=true
fi

if [ "${CONFLICT_AVAILABLE}" = true ]; then
    echo "[2/2] Running CONFLICT-AWARE allocator ..."
    "${SNIPER_ROOT}/run-sniper" \
        -c "${CONFIG}" \
        -d "${RESULTS_CONFLICT}" \
        -n 1 \
        -s "stop-by-icount:${ICOUNT}" \
        -g "perf_model/mimicos_host/memory_allocator_type=conflict_aware" \
        -g "perf_model/reserve_thp/target_fragmentation=0.0" \
        --traces="${TRACE}" \
        > "${RESULTS_CONFLICT}.stdout.log" 2>&1 || {
            echo "ERROR: Conflict-aware simulation failed. Check ${RESULTS_CONFLICT}.stdout.log"
            exit 1
        }
    echo "  Done. Results in ${RESULTS_CONFLICT}"
else
    echo "[2/2] SKIPPED: Conflict-aware allocator not yet integrated."
    echo "       Complete the exercise and rebuild Sniper to enable."
    echo "       (See README.md Step 3 for instructions.)"
fi

# ---- Print results ----
echo ""
echo "============================================================"
echo "  BASELINE RESULTS"
echo "============================================================"

IPC_BASE=$(extract_from_simout "${RESULTS_BASELINE}" "IPC")
PTW_BASE=$(extract_stat "${RESULTS_BASELINE}" "mmu.page_table_walks")
DRAM_BASE=$(extract_stat "${RESULTS_BASELINE}" "mmu_base.DRAM_accesses")
WALK_LAT_BASE=$(extract_stat "${RESULTS_BASELINE}" "mmu.total_walk_latency")
PT_PAGES_BASE=$(extract_stat "${RESULTS_BASELINE}" "page_table_pages_used")

printf "  %-35s  %15s\n" "Metric" "Baseline"
printf "  %-35s  %15s\n" "-----------------------------------" "---------------"
printf "  %-35s  %15s\n" "IPC" "${IPC_BASE}"
printf "  %-35s  %15s\n" "Page Table Walks" "${PTW_BASE}"
printf "  %-35s  %15s\n" "DRAM Accesses (PTW)" "${DRAM_BASE}"
printf "  %-35s  %15s\n" "Total Walk Latency" "${WALK_LAT_BASE}"
printf "  %-35s  %15s\n" "Page Table Pages Used" "${PT_PAGES_BASE}"

if [ "${CONFLICT_AVAILABLE}" = true ] && [ -d "${RESULTS_CONFLICT}" ]; then
    echo ""
    echo "============================================================"
    echo "  COMPARISON: Baseline vs Conflict-Aware"
    echo "============================================================"

    IPC_CA=$(extract_from_simout "${RESULTS_CONFLICT}" "IPC")
    PTW_CA=$(extract_stat "${RESULTS_CONFLICT}" "mmu.page_table_walks")
    DRAM_CA=$(extract_stat "${RESULTS_CONFLICT}" "mmu_base.DRAM_accesses")
    WALK_LAT_CA=$(extract_stat "${RESULTS_CONFLICT}" "mmu.total_walk_latency")
    PT_PAGES_CA=$(extract_stat "${RESULTS_CONFLICT}" "page_table_pages_used")
    CONFLICTS_CA=$(extract_stat "${RESULTS_CONFLICT}" "pt_allocs_conflict")
    AVOIDED_CA=$(extract_stat "${RESULTS_CONFLICT}" "pt_allocs_avoided")
    FALLBACK_CA=$(extract_stat "${RESULTS_CONFLICT}" "pt_allocs_fallback")

    printf "  %-35s  %15s  %15s\n" "Metric" "Baseline" "Conflict-Aware"
    printf "  %-35s  %15s  %15s\n" "-----------------------------------" "---------------" "---------------"
    printf "  %-35s  %15s  %15s\n" "IPC" "${IPC_BASE}" "${IPC_CA}"
    printf "  %-35s  %15s  %15s\n" "Page Table Walks" "${PTW_BASE}" "${PTW_CA}"
    printf "  %-35s  %15s  %15s\n" "DRAM Accesses (PTW)" "${DRAM_BASE}" "${DRAM_CA}"
    printf "  %-35s  %15s  %15s\n" "Total Walk Latency" "${WALK_LAT_BASE}" "${WALK_LAT_CA}"
    printf "  %-35s  %15s  %15s\n" "Page Table Pages Used" "${PT_PAGES_BASE}" "${PT_PAGES_CA}"
    printf "  %-35s  %15s  %15s\n" "Conflicts Detected" "N/A" "${CONFLICTS_CA}"
    printf "  %-35s  %15s  %15s\n" "Conflicts Avoided" "N/A" "${AVOIDED_CA}"
    printf "  %-35s  %15s  %15s\n" "Fallback (gave up)" "N/A" "${FALLBACK_CA}"
fi

echo "============================================================"
echo ""
echo "Next steps:"
echo "  1. Complete skeleton.h (fill in the three TODOs)"
echo "  2. Copy to simulator/sniper/include/memory_management/physical_memory_allocators/"
echo "  3. Rebuild Sniper and rerun this script"
