#!/bin/bash
# venv.sh — resolve the AE Python virtualenv and put it first on PATH.
#
# All Python the harness runs lives in a self-contained virtualenv, so nothing
# touches system Python — no PEP 668 "externally-managed-environment" errors on
# Ubuntu 24.04+, and no --break-system-packages. The venv holds:
#   PyYAML            — jobfile generation (create_experiments.py)
#   numpy, matplotlib — the plotters
#   huggingface_hub   — the `hf` trace-download CLI
#
# install_deps.sh creates and populates it; every other harness entry point
# (ae_common.sh, build_and_validate.sh) sources this file so
# `python3` and `hf` resolve to the venv. Override the location with AE_VENV=/path.
#
# Sourced, not executed.
AE_VENV="${AE_VENV:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.venv}"
if [ -x "$AE_VENV/bin/python3" ]; then
  export VIRTUAL_ENV="$AE_VENV"
  export PATH="$AE_VENV/bin:$PATH"
fi
