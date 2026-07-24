#!/bin/bash
# install_deps.sh — install the system packages needed to BUILD lib/sniper and
# RUN the trace-driven artifact experiments.
#
# This is the trace-replay build: it does NOT need Intel Pin / SDE (those are
# only for live-binary instrumentation). The instruction decoder (libxed) is
# bundled with the artifact, so no i386/Pin packages are required.
#
# Debian/Ubuntu (apt). Run as root or with sudo available:
#   sudo experiments/ae/lib/install_deps.sh
set -euo pipefail

PKGS=(
  # toolchain
  build-essential g++ make cmake automake autoconf libtool pkg-config
  git wget curl
  # libraries lib/sniper links against (see `ldd lib/sniper`)
  libboost-dev          # boost headers (smt_timer, branch predictors, ...)
  libsqlite3-dev        # sim.stats sqlite backend
  zlib1g-dev            # -lz  (trace .gz)
  libbz2-dev            # -lbz2
  liblzma-dev           # -llzma
  libexpat1-dev         # libexpat
  python3 python3-venv python3-dev  # python3 + venv (all harness Python lives in a local venv)
)

# All Python the harness needs goes into a self-contained virtualenv (created
# below), so nothing touches system Python — no PEP 668 / externally-managed
# error on Ubuntu 24.04+, and no --break-system-packages.
#   PyYAML            — jobfile generation (create_experiments.py)
#   numpy, matplotlib — the plotters
#   huggingface_hub   — the `hf` trace-download CLI
PIP_PKGS=(pyyaml numpy matplotlib huggingface_hub)

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This installer targets Debian/Ubuntu (apt-get)."
  echo "On other distros install the equivalents of:"
  printf '  %s\n' "${PKGS[@]}"
  exit 1
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then SUDO="sudo"; else
    echo "Run as root or install sudo."; exit 1; fi
fi

echo "==> apt-get update"
$SUDO apt-get update
echo "==> installing ${#PKGS[@]} apt packages"
$SUDO apt-get install -y "${PKGS[@]}"

# --- Python virtualenv (self-contained; no system-Python changes) ------------
# Create/populate it as the invoking user (NOT with sudo) so the repo owns it.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/venv.sh"   # sets AE_VENV
echo "==> creating Python virtualenv: $AE_VENV"
python3 -m venv "$AE_VENV"
echo "==> installing Python packages into the venv: ${PIP_PKGS[*]}"
"$AE_VENV/bin/python" -m pip install --upgrade -q pip
"$AE_VENV/bin/python" -m pip install --no-cache-dir -U "${PIP_PKGS[@]}" \
  || { echo "ERROR: could not install Python packages into the venv." >&2; exit 1; }
echo "   venv ready: $("$AE_VENV/bin/python" -V);  hf $("$AE_VENV/bin/hf" version 2>/dev/null | head -1 || echo installed)"

echo "==> done. Next:  bash experiments/ae/build_and_validate.sh --skip-deps"
