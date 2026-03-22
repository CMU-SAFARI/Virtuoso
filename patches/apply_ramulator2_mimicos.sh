#!/bin/bash
# Apply MimicOS integration patch to Ramulator2 submodule
# Run from the repository root: bash patches/apply_ramulator2_mimicos.sh
#
# This script:
# 1. Applies the Ramulator2 translation + pintool patch
# 2. Symlinks the shared MimicOS from repo root into Ramulator2
# 3. Creates symlinks to shared allocator headers (sniper/include/)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RAMULATOR2_DIR="$ROOT_DIR/simulator/ramulator2"
PATCH_FILE="$SCRIPT_DIR/ramulator2_mimicos.patch"

echo "=== Virtuoso: Applying MimicOS integration to Ramulator2 ==="

# Check submodule is initialized
if [ ! -f "$RAMULATOR2_DIR/CMakeLists.txt" ]; then
    echo "Initializing Ramulator2 submodule..."
    git -C "$ROOT_DIR" submodule update --init simulator/ramulator2
fi

# Step 1: Apply the translation + pintool patch
echo "[1/3] Applying Ramulator2 patch (translation, pintool, tests)..."
cd "$RAMULATOR2_DIR"
git apply "$PATCH_FILE"

# Step 2: Symlink shared MimicOS from repo root
echo "[2/3] Symlinking shared MimicOS..."
if [ -d mimicos ] && [ ! -L mimicos ]; then
    echo "  Warning: removing existing mimicos/ directory"
    rm -rf mimicos
fi
ln -sfn ../../mimicos mimicos

# Step 3: Verify the shared headers are accessible
echo "[3/3] Verifying shared header access..."
if [ ! -f mimicos/include/mm/allocator_factory.h ]; then
    echo "  ERROR: Shared MimicOS headers not found. Is mimicos/ at repo root?"
    exit 1
fi

# Check if SNIPER_INCLUDE is needed for allocator templates
if [ -f "$ROOT_DIR/simulator/sniper/include/memory_management/physical_memory_allocators/physical_memory_allocator.h" ]; then
    echo "  Sniper include/ headers found."
else
    echo "  WARNING: sniper/include/ not found. MimicOS build may fail."
    echo "  Set SNIPER_INCLUDE when building: make SNIPER_INCLUDE=/path/to/sniper/include"
fi

echo ""
echo "=== Done. Ramulator2 MimicOS integration ready. ==="
echo ""
echo "To build MimicOS standalone:"
echo "  cd simulator/ramulator2/mimicos"
echo "  make SNIPER_INCLUDE=\$PWD/../../sniper/include"
echo ""
echo "To build Ramulator2 (with TLB/MimicOS translation):"
echo "  cd simulator/ramulator2 && mkdir -p build && cd build && cmake .. && make -j"
echo ""
echo "To build the SDE pintool (for MimicOS IPC bridge):"
echo "  cd simulator/ramulator2/pintool"
echo "  make SDE_BUILD_KIT=/path/to/sde_kit"
