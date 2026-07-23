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
  python3 python3-dev   # embedded python (libpython3.x) + harness scripts
)

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
echo "==> installing ${#PKGS[@]} packages"
$SUDO apt-get install -y "${PKGS[@]}"
echo "==> done. You can now build:  experiments/ae/run_ae.sh --build --mode local --claim <claim>"
