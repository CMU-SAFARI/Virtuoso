#!/bin/bash
# Virtuoso Quick Example
# Runs a short simulation using binary instrumentation (PIN/SDE) with the `ls` command.
# Uses the modular config system with ReserveTHP allocator.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CONFIG=config/address_translation_schemes/reservethp.cfg
OUTPUT=./example_output
WORKLOAD=ls

echo "=== Virtuoso Example Simulation ==="
echo "Config: $CONFIG"
echo "Workload: $WORKLOAD"
echo ""

./run-sniper \
    -c "$CONFIG" \
    -d "$OUTPUT" \
    --genstats \
    -- $WORKLOAD

# Check result
if [ -f "$OUTPUT/sim.info" ]; then
    echo ""
    echo "=== Simulation completed successfully ==="
    grep "IPC\|Instructions\|Cycles" "$OUTPUT/simulation/sim.out" 2>/dev/null | head -5
else
    echo ""
    echo "=== Simulation failed. Check output in $OUTPUT ==="
    exit 1
fi

# Cleanup
rm -rf "$OUTPUT"
