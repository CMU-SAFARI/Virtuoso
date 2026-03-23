#!/bin/bash
# run_demo.sh -- Part 2: Compare baseline vs HW fault handler
#
# Runs two Virtuoso simulations with identical workloads:
#   1. Baseline:   mmu_base  -- all page faults handled by OS (2000-cycle trap)
#   2. HW Faults:  mmu_hw_fault -- HW pool handles faults in 50ns when possible,
#                  falls back to OS for the rest
#
# Both simulations charge page fault latency (page_fault_latency=2000 cycles)
# so the comparison is fair.
#
# Usage:
#   cd demos/part2_hw_faults
#   bash run_demo.sh [trace_path] [instruction_count]
#
# Examples:
#   bash run_demo.sh /path/to/traces/rnd.sift
#   bash run_demo.sh /path/to/traces/rnd.sift 5000000

set -e

# ---- Resolve paths ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SNIPER_ROOT="${REPO_ROOT}/simulator/sniper"

if [ ! -x "${SNIPER_ROOT}/run-sniper" ]; then
    echo "ERROR: Cannot find run-sniper at ${SNIPER_ROOT}/run-sniper"
    echo "       Make sure Virtuoso is built (cd simulator/sniper && make -j4)."
    exit 1
fi

# ---- Trace selection ----
TRACE="${1:-}"
if [ -z "${TRACE}" ]; then
    if [ -n "${VIRTUOSO_TRACES}" ] && [ -d "${VIRTUOSO_TRACES}" ]; then
        TRACE="${VIRTUOSO_TRACES}/rnd.sift"
    else
        echo "ERROR: No trace specified and VIRTUOSO_TRACES is not set."
        echo "Usage: $0 <path/to/trace.sift> [instruction_count]"
        exit 1
    fi
fi

if [ ! -f "${TRACE}" ] && [ ! -f "${TRACE}.sift" ]; then
    echo "ERROR: Trace not found: ${TRACE}"
    exit 1
fi

# ---- Configuration ----
CONFIG="${SNIPER_ROOT}/config/address_translation_schemes/reservethp.cfg"
ICOUNT="${2:-2000000}"
PAGE_FAULT_LATENCY=2000  # cycles charged per OS-handled page fault
RESULTS_BASELINE="${SCRIPT_DIR}/results_baseline"
RESULTS_HWFAULTS="${SCRIPT_DIR}/results_hwfaults"

echo "============================================================"
echo "  Part 2: Baseline vs HW Fault Handler"
echo "============================================================"
echo "  Sniper root         : ${SNIPER_ROOT}"
echo "  Config              : ${CONFIG}"
echo "  Trace               : ${TRACE}"
echo "  Instructions        : ${ICOUNT}"
echo "  Page fault latency  : ${PAGE_FAULT_LATENCY} cycles"
echo ""

# ---- Clean previous results ----
rm -rf "${RESULTS_BASELINE}" "${RESULTS_HWFAULTS}"

# ---- Helper: find sim.stats / sim.out under results dir ----
# Sniper puts output in a simulation/ subdirectory
find_file() {
    local dir="$1" name="$2"
    if [ -f "${dir}/simulation/${name}" ]; then echo "${dir}/simulation/${name}"
    elif [ -f "${dir}/${name}" ]; then echo "${dir}/${name}"
    else echo ""; fi
}

extract_metric() {
    local dir="$1" metric="$2"
    local stats_file
    stats_file=$(find_file "${dir}" "sim.stats")
    [ -z "${stats_file}" ] && { echo "N/A"; return; }
    grep -m1 "^${metric} " "${stats_file}" 2>/dev/null | awk -F'= ' '{print $2}' || echo "N/A"
}

extract_ipc() {
    local dir="$1"
    local simout
    simout=$(find_file "${dir}" "sim.out")
    [ -z "${simout}" ] && { echo "N/A"; return; }
    grep -m1 "IPC" "${simout}" 2>/dev/null | awk '{print $NF}' || echo "N/A"
}

# ---- Run baseline ----
echo "[1/2] Running BASELINE (mmu_base, page_fault_latency=${PAGE_FAULT_LATENCY}) ..."
"${SNIPER_ROOT}/run-sniper" \
    -c "${CONFIG}" \
    -d "${RESULTS_BASELINE}" \
    -n 1 \
    -s "stop-by-icount:${ICOUNT}" \
    -g "perf_model/reserve_thp/target_fragmentation=0.0" \
    -g "perf_model/mimicos_host/page_fault_latency=${PAGE_FAULT_LATENCY}" \
    --traces="${TRACE}" \
    > "${RESULTS_BASELINE}.stdout.log" 2>&1 || {
        echo "ERROR: Baseline simulation failed. Check ${RESULTS_BASELINE}.stdout.log"
        exit 1
    }
echo "  Done. Results in ${RESULTS_BASELINE}"

# ---- Run hw_faults ----
echo "[2/2] Running HW_FAULTS (mmu_hw_fault, pool=160MB, hw_latency=50ns) ..."
"${SNIPER_ROOT}/run-sniper" \
    -c "${CONFIG}" \
    -d "${RESULTS_HWFAULTS}" \
    -n 1 \
    -s "stop-by-icount:${ICOUNT}" \
    -g "perf_model/mmu/type=hw_fault" \
    -g "perf_model/reserve_thp/target_fragmentation=0.0" \
    -g "perf_model/mimicos_host/page_fault_latency=${PAGE_FAULT_LATENCY}" \
    --traces="${TRACE}" \
    > "${RESULTS_HWFAULTS}.stdout.log" 2>&1 || {
        echo "ERROR: HW faults simulation failed. Check ${RESULTS_HWFAULTS}.stdout.log"
        exit 1
    }
echo "  Done. Results in ${RESULTS_HWFAULTS}"

# ---- Extract and display metrics ----
echo ""
echo "============================================================"
echo "  COMPARISON TABLE"
echo "============================================================"

IPC_BASE=$(extract_ipc "${RESULTS_BASELINE}")
IPC_HW=$(extract_ipc "${RESULTS_HWFAULTS}")

PF_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.page_faults")
PF_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.page_faults")
PF_HW_HANDLED=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.page_faults_hw_handled")
PF_OS_HANDLED=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.page_faults_os_handled")

FAULT_LAT_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.total_fault_latency")
FAULT_LAT_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.total_fault_latency")
HW_FAULT_LAT=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.total_hw_fault_latency")

WALK_LAT_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.total_table_walk_latency")
WALK_LAT_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.total_table_walk_latency")

TRANS_LAT_BASE=$(extract_metric "${RESULTS_BASELINE}" "mmu.total_translation_latency")
TRANS_LAT_HW=$(extract_metric "${RESULTS_HWFAULTS}" "mmu.total_translation_latency")

printf "  %-30s  %18s  %18s\n" "Metric" "Baseline" "HW Faults"
printf "  %-30s  %18s  %18s\n" "------------------------------" "------------------" "------------------"
printf "  %-30s  %18s  %18s\n" "IPC" "${IPC_BASE}" "${IPC_HW}"
printf "  %-30s  %18s  %18s\n" "Page Faults" "${PF_BASE}" "${PF_HW}"
printf "  %-30s  %18s  %18s\n" "  HW-handled" "-" "${PF_HW_HANDLED}"
printf "  %-30s  %18s  %18s\n" "  OS-handled" "${PF_BASE}" "${PF_OS_HANDLED}"
printf "  %-30s  %18s  %18s\n" "Total Fault Latency" "${FAULT_LAT_BASE}" "${FAULT_LAT_HW}"
printf "  %-30s  %18s  %18s\n" "  HW Fault Latency" "-" "${HW_FAULT_LAT}"
printf "  %-30s  %18s  %18s\n" "Total Walk Latency" "${WALK_LAT_BASE}" "${WALK_LAT_HW}"
printf "  %-30s  %18s  %18s\n" "Total Translation Latency" "${TRANS_LAT_BASE}" "${TRANS_LAT_HW}"
echo "============================================================"

echo ""
echo "For detailed plots, run:"
echo "  python3 compare.py ${RESULTS_BASELINE} ${RESULTS_HWFAULTS} --out-dir plots/"
