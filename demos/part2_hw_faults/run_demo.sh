#!/bin/bash
# run_demo.sh -- Part 2: Compare baseline vs HW fault handler
#
# Runs two Virtuoso simulations:
#   1. Baseline:   mmu_base + ReserveTHP (frag=0.0)
#   2. HW Faults:  mmu_hw_fault + ReserveTHP (frag=0.0)
#
# Prints a comparison table of IPC, page faults, and translation latency.
#
# Usage:
#   cd demos/part2_hw_faults
#   bash run_demo.sh [trace_path]
#
# The trace defaults to ${VIRTUOSO_TRACES}/rnd.sift if not specified.

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
RESULTS_HWFAULTS="${SCRIPT_DIR}/results_hwfaults"

echo "============================================================"
echo "  Part 2: Baseline vs HW Fault Handler"
echo "============================================================"
echo "  Sniper root : ${SNIPER_ROOT}"
echo "  Config      : ${CONFIG}"
echo "  Trace       : ${TRACE}"
echo "  Instructions: ${ICOUNT}"
echo ""

# ---- Clean previous results ----
rm -rf "${RESULTS_BASELINE}" "${RESULTS_HWFAULTS}"

# ---- Run baseline ----
echo "[1/2] Running BASELINE (mmu_base, ReserveTHP, frag=0.0) ..."
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

# ---- Run hw_faults ----
echo "[2/2] Running HW_FAULTS (mmu_hw_fault, ReserveTHP, frag=0.0) ..."
"${SNIPER_ROOT}/run-sniper" \
    -c "${CONFIG}" \
    -d "${RESULTS_HWFAULTS}" \
    -n 1 \
    -s "stop-by-icount:${ICOUNT}" \
    -g "perf_model/mmu/type=hw_fault" \
    -g "perf_model/mmu/count_page_fault_latency=true" \
    -g "perf_model/reserve_thp/target_fragmentation=0.0" \
    --traces="${TRACE}" \
    > "${RESULTS_HWFAULTS}.stdout.log" 2>&1 || {
        echo "ERROR: HW faults simulation failed. Check ${RESULTS_HWFAULTS}.stdout.log"
        exit 1
    }
echo "  Done. Results in ${RESULTS_HWFAULTS}"

# ---- Extract metrics ----
echo ""
echo "============================================================"
echo "  Extracting metrics ..."
echo "============================================================"

extract_metric() {
    # Usage: extract_metric <results_dir> <metric_name>
    local dir="$1"
    local metric="$2"
    local stats_file="${dir}/sim.stats"
    if [ ! -f "${stats_file}" ]; then
        echo "N/A"
        return
    fi
    # Try to grep the metric from sim.stats (Sniper stats format)
    local val
    val=$(grep -m1 "${metric}" "${stats_file}" 2>/dev/null | awk '{print $NF}' || echo "N/A")
    echo "${val}"
}

# Use Sniper's built-in stats parser if available
if [ -f "${SNIPER_ROOT}/tools/sniper_lib.py" ]; then
    PYTHONPATH="${SNIPER_ROOT}/tools:${PYTHONPATH:-}"
    export PYTHONPATH
fi

# Extract key stats from sim.out (human-readable summary)
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

IPC_BASE=$(extract_from_simout "${RESULTS_BASELINE}" "IPC")
IPC_HW=$(extract_from_simout "${RESULTS_HWFAULTS}" "IPC")

PF_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.page_faults")
PF_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.page_faults")

WALK_LAT_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.total_walk_latency")
WALK_LAT_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.total_walk_latency")

TRANS_LAT_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.total_translation_latency")
TRANS_LAT_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.total_translation_latency")

FAULT_LAT_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.total_fault_latency")
FAULT_LAT_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.total_fault_latency")

# ---- Print comparison table ----
echo ""
echo "============================================================"
echo "  COMPARISON TABLE"
echo "============================================================"
printf "  %-30s  %15s  %15s\n" "Metric" "Baseline" "HW Faults"
printf "  %-30s  %15s  %15s\n" "------------------------------" "---------------" "---------------"
printf "  %-30s  %15s  %15s\n" "IPC" "${IPC_BASE}" "${IPC_HW}"
printf "  %-30s  %15s  %15s\n" "Page Faults" "${PF_BASE}" "${PF_HW}"
printf "  %-30s  %15s  %15s\n" "Total Walk Latency" "${WALK_LAT_BASE}" "${WALK_LAT_HW}"
printf "  %-30s  %15s  %15s\n" "Total Translation Latency" "${TRANS_LAT_BASE}" "${TRANS_LAT_HW}"
printf "  %-30s  %15s  %15s\n" "Total Fault Latency" "${FAULT_LAT_BASE}" "${FAULT_LAT_HW}"
echo "============================================================"

echo ""
echo "For detailed plots, run:"
echo "  python3 compare.py ${RESULTS_BASELINE} ${RESULTS_HWFAULTS}"
