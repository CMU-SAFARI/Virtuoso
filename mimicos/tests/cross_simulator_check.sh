#!/bin/bash
# Drive gem5 and ChampSim with one virtual address stream and check that
# MimicOS makes the same decisions in both.
#
# The two simulators cannot read the same input: gem5 runs a binary,
# ChampSim reads a trace.  So run the binary in gem5 first, take the
# pages it touched, and build a ChampSim trace that touches the same
# pages.
#
# They will not fault in the same order, and do not have to.  MimicOS
# hands out frames as faults arrive, so a different order means the same
# pages get different frames, exactly as a real OS would.  What is
# checked is what does not depend on the order: both map the same pages,
# at the same sizes, one frame each, and no page moves within a run.
#
# Set WORKLOAD_OPTIONS to pass arguments to the workload.  Keep the
# working set small: the check only needs the mappings, and a realistic
# fault cost makes ChampSim simulate a very long time for a large one.
#
# Usage: cross_simulator_check.sh <gem5.opt> <champsim> <workload> [configs...]

set -u

GEM5="${1:?usage: $0 <gem5.opt> <champsim> <workload> [configs...]}"
CHAMPSIM="${2:?}"
WORKLOAD="${3:?}"
shift 3

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MIMICOS="$(cd "$HERE/.." && pwd)"
CONFIGS=("$@")
if [ ${#CONFIGS[@]} -eq 0 ]; then
    CONFIGS=("$MIMICOS/configs/embedded_4gb_4k.ini"
             "$MIMICOS/configs/embedded_4gb_mixed.ini"
             "$MIMICOS/configs/embedded_4gb_2m.ini")
fi

GEM5_ROOT="$(cd "$(dirname "$GEM5")/../.." && pwd)"
SCRIPT="$GEM5_ROOT/configs/example/mimicos_se.py"
WORK="$(mktemp -d)"
FAILURES=0

echo "workload:  $WORKLOAD"
echo "scratch:   $WORK"
echo

for CONFIG in "${CONFIGS[@]}"; do
    NAME="$(basename "$CONFIG" .ini)"
    echo "=== $NAME ==="

    GEM5_MAP="$WORK/$NAME.gem5.csv"
    CS_MAP="$WORK/$NAME.champsim.csv"
    VPNS="$WORK/$NAME.vpns"
    CS_TRACE="$WORK/$NAME.champsimtrace"

    MIMICOS_MAPPING_TRACE="$GEM5_MAP" "$GEM5" --outdir="$WORK/m5_$NAME" \
        "$SCRIPT" --cpu-type atomic --mimicos-config "$CONFIG" \
        --options "${WORKLOAD_OPTIONS:-}" "$WORKLOAD" \
        > "$WORK/$NAME.gem5.log" 2>&1
    if [ ! -s "$GEM5_MAP" ]; then
        echo "  FAIL: gem5 produced no mapping trace (see $WORK/$NAME.gem5.log)"
        FAILURES=$((FAILURES + 1))
        continue
    fi
    echo "  gem5:     $(($(grep -vc '^#' "$GEM5_MAP") - 1)) mappings"

    # The address stream gem5 faulted on, in order.
    awk -F, '$0 !~ /^#/ && $1 != "seq" {print $2, $3}' "$GEM5_MAP" > "$VPNS"

    python3 "$HERE/va_to_champsim_trace.py" "$VPNS" "$CS_TRACE" \
        > "$WORK/$NAME.gen.log" 2>&1 || {
        echo "  FAIL: could not build a ChampSim trace"
        FAILURES=$((FAILURES + 1))
        continue
    }

    # ChampSim loops a trace that runs out, so stop at its real length
    # or the run never ends.
    NINSTR=$(awk '/^wrote /{print $2}' "$WORK/$NAME.gen.log")
    : "${NINSTR:=100000}"

    MIMICOS_CONFIG="$CONFIG" MIMICOS_MAPPING_TRACE="$CS_MAP" \
        "$CHAMPSIM" --deadlock-cycle 2000000 -w 0 -i "$NINSTR" "$CS_TRACE" \
        > "$WORK/$NAME.champsim.log" 2>&1
    if [ ! -s "$CS_MAP" ]; then
        echo "  FAIL: ChampSim produced no mapping trace (see $WORK/$NAME.champsim.log)"
        FAILURES=$((FAILURES + 1))
        continue
    fi
    echo "  ChampSim: $(($(grep -vc '^#' "$CS_MAP") - 1)) mappings"

    python3 "$HERE/check_equivalence.py" --compare "$GEM5_MAP" "$CS_MAP" \
        --unordered --ignore-asid > "$WORK/$NAME.cmp" 2>&1
    CMP_RC=$?
    sed 's/^/  /' "$WORK/$NAME.cmp"
    [ "$CMP_RC" -ne 0 ] && FAILURES=$((FAILURES + 1))
    echo
done

if [ "$FAILURES" -ne 0 ]; then
    echo "$FAILURES configuration(s) FAILED (artifacts in $WORK)"
    exit 1
fi

echo "gem5 and ChampSim map the same pages at the same sizes, for every page size"
rm -rf "$WORK"
