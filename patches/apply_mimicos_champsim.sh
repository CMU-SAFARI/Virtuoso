#!/bin/bash
# Apply the MimicOS integration to ChampSim.
# Run from the repository root: bash patches/apply_mimicos_champsim.sh [champsim_dir]
#
# What you get: MimicOS decides which frame backs a page, how large that page
# is, what a minor fault costs, and where every page-table entry on the walk
# physically lives -- so ChampSim's PageTableWalker fetches those addresses
# through the real cache hierarchy. See docs/mimicos_portable.md.

set -e

CHAMPSIM_COMMIT="06de8d3"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CHAMPSIM_DIR="${1:-$ROOT_DIR/simulator/ChampSim}"
PATCH_DIR="$SCRIPT_DIR/champsim/mimicos"
MIMICOS_DIR="$ROOT_DIR/mimicos"

echo "=== Virtuoso: Applying MimicOS integration to ChampSim ==="
echo "    ChampSim: $CHAMPSIM_DIR"
echo "    patches:  $PATCH_DIR"

# ChampSim is not vendored here, so fetch it at the pinned commit if it
# is missing.  The patch was generated against that commit.
if [ ! -d "$CHAMPSIM_DIR" ]; then
    echo "[0/3] Cloning ChampSim..."
    git clone https://github.com/ChampSim/ChampSim.git "$CHAMPSIM_DIR"
    git -C "$CHAMPSIM_DIR" checkout "$CHAMPSIM_COMMIT"
    git -C "$CHAMPSIM_DIR" submodule update --init
fi

# Step 1: apply the patch series
echo "[1/3] Applying MimicOS patches..."
cd "$CHAMPSIM_DIR"
if git log --oneline | grep -q "mimicos: let MimicOS own physical memory"; then
    echo "  Already applied; skipping."
else
    if ! git merge-base --is-ancestor "$CHAMPSIM_COMMIT" HEAD 2>/dev/null; then
        echo "  Warning: $CHAMPSIM_DIR does not contain $CHAMPSIM_COMMIT. The"
        echo "           patches were generated against it and may not apply cleanly."
    fi
    git am "$PATCH_DIR"/*.patch
fi

# Step 2: build the MimicOS embedding library
echo "[2/3] Building libmimicos..."
make -C "$MIMICOS_DIR" lib-static

if [ ! -f "$MIMICOS_DIR/build/libmimicos.a" ]; then
    echo "  ERROR: $MIMICOS_DIR/build/libmimicos.a was not produced."
    exit 1
fi

echo "[3/3] Done."
cat <<EOF

Next steps:

  cd $CHAMPSIM_DIR
  ./vcpkg/bootstrap-vcpkg.sh && ./vcpkg/vcpkg install   # first time only
  ./config.sh champsim_config.json
  make MIMICOS_HOME=$MIMICOS_DIR -j\$(nproc)

  MIMICOS_CONFIG=$MIMICOS_DIR/configs/embedded_4gb_thp.ini \\
    ./bin/champsim --deadlock-cycle 100000 -w 200000 -i 1000000 trace.champsimtrace.xz

--deadlock-cycle matters: a measured minor page fault is microseconds, i.e.
thousands of cycles, and ChampSim's stock 500-cycle deadlock window reports
that as a deadlock.

Run the tests:

  make test MIMICOS_HOME=$MIMICOS_DIR -j\$(nproc) && ./test/bin/000-test-main

Check the adapter is a pass-through:

  MIMICOS_CONFIG=$MIMICOS_DIR/configs/embedded_4gb_thp.ini \\
  MIMICOS_MAPPING_TRACE=/tmp/champsim_mapping.csv \\
    ./bin/champsim --deadlock-cycle 100000 -w 20000 -i 100000 trace.champsimtrace.xz

  python3 $MIMICOS_DIR/tests/check_equivalence.py \\
      --config $MIMICOS_DIR/configs/embedded_4gb_thp.ini \\
      --trace  /tmp/champsim_mapping.csv

Note: do not rebuild libmimicos.a while ChampSim is linking against it.

EOF
