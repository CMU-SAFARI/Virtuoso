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

# --- embedded-Python dev library ---------------------------------------------
# The simulator links the embedded CPython via `python3-config --libs --embed`
# (common/Makefile.common). If `python3` is a non-default build (e.g. 3.13 from a
# PPA) whose -dev lib is absent, the final link fails with `cannot find -lpythonX.Y`.
# Make sure the dev lib MATCHING python3-config is installed.
ensure_python_embed() {
  local cc pyv pycfg
  # match the OS python3-config the build pins to (build_and_validate.sh)
  pycfg=$(command -v /usr/bin/python3-config || command -v python3-config || echo python3-config)
  cc=$(command -v gcc || command -v cc || echo gcc)
  pyv=$("$pycfg" --includes 2>/dev/null | grep -oE 'python3\.[0-9]+t?' | head -1 | sed 's/python//')
  _links() { echo 'int main(void){return 0;}' | "$cc" -xc - \
      $("$pycfg" --includes 2>/dev/null) $("$pycfg" --ldflags --embed 2>/dev/null) \
      -o /tmp/_ae_pyembed 2>/dev/null; local r=$?; rm -f /tmp/_ae_pyembed; return $r; }
  if _links; then echo "==> embedded Python OK (python ${pyv})"; return 0; fi
  echo "==> embedded-Python dev lib for python ${pyv:-?} missing — installing it"
  $SUDO apt-get install -y "libpython${pyv}-dev" 2>/dev/null \
    || $SUDO apt-get install -y "python${pyv}-dev" 2>/dev/null || true
  if _links; then echo "   ok (python ${pyv})"; return 0; fi
  echo "WARNING: python3-config reports Python ${pyv}, but its dev library (-lpython${pyv}) is missing" >&2
  echo "         and could not be auto-installed. Install it manually, e.g.:" >&2
  echo "           $SUDO apt-get install libpython${pyv}-dev   # or  python${pyv}-dev" >&2
  echo "         The simulator links -lpython${pyv} (common/Makefile.common)." >&2
}
ensure_python_embed

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
