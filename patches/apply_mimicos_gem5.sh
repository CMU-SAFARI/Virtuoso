#!/bin/bash
# Apply the MimicOS syscall-emulation-mode integration to gem5.
# Run from the repository root: bash patches/apply_mimicos_gem5.sh [gem5_dir]
#
# gem5 is not vendored in this artifact -- the integration ships as a patch
# against a pinned upstream release, so you apply it to your own checkout.
#
# What you get: MimicOS decides which physical frame backs each page of process
# memory and what a minor fault costs, replacing gem5's bump-pointer MemPool
# and its zero-cost faults. See docs/mimicos_portable.md.

set -e

GEM5_TAG="v24.1.0.3"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GEM5_DIR="${1:-$ROOT_DIR/simulator/gem5}"
PATCH_DIR="$SCRIPT_DIR/gem5"
MIMICOS_DIR="$ROOT_DIR/mimicos"

echo "=== Virtuoso: Applying MimicOS integration to gem5 ==="
echo "    gem5:    $GEM5_DIR"
echo "    patches: $PATCH_DIR"

# Step 1: obtain gem5 at the pinned tag if it is not already there
if [ ! -d "$GEM5_DIR" ]; then
    echo "[1/4] Cloning gem5 $GEM5_TAG..."
    git clone --depth 1 --branch "$GEM5_TAG" https://github.com/gem5/gem5.git "$GEM5_DIR"
else
    echo "[1/4] Using existing gem5 checkout"
    if ! git -C "$GEM5_DIR" rev-parse --verify "$GEM5_TAG" >/dev/null 2>&1; then
        echo "  Warning: $GEM5_DIR is not at $GEM5_TAG. The patches were generated"
        echo "           against that tag and may not apply cleanly."
    fi
fi

# Step 2: apply the patch series
echo "[2/4] Applying MimicOS patches..."
cd "$GEM5_DIR"
if git log --oneline | grep -q "mimicos: let MimicOS place physical memory"; then
    echo "  Already applied; skipping."
else
    git am "$PATCH_DIR"/*.patch
fi

# Step 3: build the MimicOS embedding library
echo "[3/4] Building libmimicos..."
make -C "$MIMICOS_DIR" lib-static

if [ ! -f "$MIMICOS_DIR/build/libmimicos.a" ]; then
    echo "  ERROR: $MIMICOS_DIR/build/libmimicos.a was not produced."
    exit 1
fi

# Step 4: tell the user how to build.  We do not run scons here: a gem5 build
# takes tens of minutes and the ISA/variant is the user's choice.
echo "[4/4] Done."
cat <<EOF

Next steps:

  cd $GEM5_DIR
  MIMICOS_HOME=$MIMICOS_DIR scons build/X86/gem5.opt -j\$(nproc)

  build/X86/gem5.opt configs/example/mimicos_se.py \\
      --mimicos-config $MIMICOS_DIR/configs/embedded_4gb_thp.ini

Verify the integration end to end:

  python3 $MIMICOS_DIR/tests/check_gem5_se.py \\
      --gem5   $GEM5_DIR/build/X86/gem5.opt \\
      --config $MIMICOS_DIR/configs/embedded_4gb_thp.ini

Note: do not rebuild libmimicos.a while gem5 is linking against it. 'ar'
rewrites the archive in place, and a link that reads it mid-write fails with
confusing .strtab errors.

EOF
