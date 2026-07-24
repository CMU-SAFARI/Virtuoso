#!/bin/bash
# ===========================================================================
# reproduce.sh — one-command Artifact-Evaluation entry point for TRAIL.
#
# A reviewer just clones the repo and runs:
#
#     bash experiments/ae/reproduce.sh
#
# It performs, with progress, everything up to and including a random-trace
# sanity test, then STOPS and tells you how to launch the full experiments:
#
#     [1/4] install system build dependencies      (apt; uses sudo if not root)
#     [2/4] build the trace-replay simulator       (no Pin/SDE/libtorch)
#     [3/4] download traces + trace-lists           (Hugging Face dataset)
#     [4/4] validate the setup on N random traces   (short sims -> valid IPC)
#
# Every phase is resumable: a finished phase is detected and skipped on re-run.
#
# Options (env var or flag):
#   --hf-repo   REPO   Hugging Face dataset      (default: $HF_REPO or konkanello/trail-tlb-traces)
#   --bundle    DIR    where to download traces  (default: <artifact>/ae_bundle)
#   --n         N      random traces to validate (default: 3)
#   --skip-deps        do not run install_deps.sh (deps already installed)
#   --skip-download    reuse an existing bundle (no HF download)
# ===========================================================================
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"                 # artifact root
SNIPER="$ROOT/simulator/sniper"

HF_REPO="${HF_REPO:-konkanello/trail-tlb-traces}"
BUNDLE="$ROOT/ae_bundle"
NVAL=3
SKIP_DEPS=0
SKIP_DL=0
while [ $# -gt 0 ]; do case "$1" in
  --hf-repo) HF_REPO="$2"; shift 2;;
  --bundle) BUNDLE="$2"; shift 2;;
  --n) NVAL="$2"; shift 2;;
  --skip-deps) SKIP_DEPS=1; shift;;
  --skip-download) SKIP_DL=1; shift;;
  -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done

C_G=$'\033[1;32m'; C_Y=$'\033[1;33m'; C_R=$'\033[1;31m'; C_0=$'\033[0m'
step() { echo; echo "${C_G}==== $* ====${C_0}"; }
die()  { echo "${C_R}ERROR: $*${C_0}" >&2; exit 1; }

echo "TRAIL artifact reproduction"
echo "  artifact root : $ROOT"
echo "  HF dataset    : $HF_REPO"
echo "  trace bundle  : $BUNDLE"

# --- [1/4] dependencies ------------------------------------------------------
step "[1/4] System build dependencies"
if [ "$SKIP_DEPS" -eq 1 ]; then
  echo "  --skip-deps: assuming build dependencies are already installed."
else
  bash "$HERE/lib/install_deps.sh" || die "dependency install failed (see above)."
fi

# --- [2/4] build -------------------------------------------------------------
step "[2/4] Build the trace-replay simulator"
if [ -x "$SNIPER/lib/sniper" ]; then
  echo "  ${C_Y}lib/sniper already built — skipping.${C_0} (delete it to force a rebuild)"
else
  echo "  building (this takes a few minutes; no Pin/SDE/libtorch downloads) ..."
  ( cd "$SNIPER" && make -j"$(nproc)" SNIPER_TRACE_ONLY=1 replay ) || die "build failed."
  [ -x "$SNIPER/lib/sniper" ] || die "build finished but lib/sniper is missing."
fi
echo "  ${C_G}lib/sniper ready.${C_0}"

# --- [3/4] traces ------------------------------------------------------------
step "[3/4] Download traces + trace-lists"
mkdir -p "$BUNDLE"
if [ "$SKIP_DL" -eq 1 ]; then
  echo "  --skip-download: reusing $BUNDLE"
else
  command -v hf >/dev/null 2>&1 || {
    echo "  installing huggingface_hub CLI ..."
    python3 -m pip install -q -U "huggingface_hub[cli]" || die "could not install huggingface_hub (need python3-pip)."
  }
  echo "  downloading dataset '$HF_REPO' -> $BUNDLE"
  echo "  ${C_Y}(this is large — the full trace set; the download is resumable, so you can re-run reproduce.sh if it is interrupted)${C_0}"
  hf download "$HF_REPO" --repo-type dataset --local-dir "$BUNDLE" \
    || die "trace download failed. If the dataset is private, run 'hf auth login' first (paste a read token)."
fi
[ -d "$BUNDLE/traces" ]   || die "no traces/ in $BUNDLE — download incomplete?"
[ -d "$BUNDLE/vm_tlist" ] || die "no vm_tlist/ in $BUNDLE — download incomplete?"
echo "  traces: $(find "$BUNDLE/traces" -type f | wc -l) files   trace-lists: $(ls "$BUNDLE"/vm_tlist/*.tlist 2>/dev/null | wc -l)"

# resolve the trace-lists (from the bundle) against the downloaded traces/ folder
echo "  resolving trace-lists -> experiments/vm_tlist ..."
bash "$HERE/lib/setup_tlists.sh" "$(realpath "$BUNDLE/traces")" "$ROOT/experiments/vm_tlist" "$BUNDLE/vm_tlist" \
  || die "setup_tlists.sh failed."

# --- [4/4] validate ----------------------------------------------------------
step "[4/4] Validate the setup on $NVAL random traces"
bash "$HERE/lib/validate_traces.sh" --n "$NVAL" || die "trace validation failed — do not launch the full run yet."

# --- done: point at the full run --------------------------------------------
echo
echo "${C_G}================================================================${C_0}"
echo "${C_G} Setup is validated. You are ready to run the experiments.${C_0}"
echo "${C_G}================================================================${C_0}"
cat <<NEXT

Run a claim in three steps (results + figures land in experiments/ae/ae_out/):

  # 2. launch all jobs for a claim (non-blocking)
  bash experiments/ae/ae_launch.sh  --claim head8mb --mode slurm --partitions <partition>
  #    (single machine, no SLURM:  --mode local --jobs \$(nproc))

  # 3. start the background watcher, then check progress whenever you like
  bash experiments/ae/ae_watch.sh   --claim head8mb
  cat  experiments/ae/ae_out/head8mb.status      # done/running/failed, any time

  # 4. once the watcher writes ae_out/head8mb.DONE, parse + plot
  bash experiments/ae/ae_results.sh --claim head8mb            # (add --wait to block)

  claims: head8mb head2mb table5 table6 pqsweep multicore

Note: a full claim is thousands of 300M-instruction sims (cluster-scale). The
random-trace check above already confirms the build, traces and simulator work.
NEXT
