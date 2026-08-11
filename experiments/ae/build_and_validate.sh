#!/bin/bash
# ===========================================================================
# build_and_validate.sh — one-command Artifact-Evaluation entry point for TRAIL.
# (installs deps, builds the trace-replay simulator, downloads traces, validates)
#
# A reviewer just clones the repo and runs:
#
#     bash experiments/ae/build_and_validate.sh
#
# It performs, with progress, everything up to and including a random-trace
# sanity test, then STOPS and tells you how to launch the full experiments:
#
#     [1/4] install system build dependencies      (apt; uses sudo if not root)
#     [2/4] build the trace-replay simulator       (no Pin/SDE/libtorch)
#     [3/4] traces + trace-lists                    (pre-staged bundle, else HF)
#     [4/4] validate the setup on N random traces   (short sims -> valid IPC)
#
# Every phase is resumable: a finished phase is detected and skipped on re-run.
#
# PRE-STAGED TRACES
#   If a valid bundle already sits one level ABOVE the artifact root — i.e.
#   <artifact>/../ae_bundle — phase [3/4] uses it and skips the download
#   entirely. This is the normal case on a machine where the traces were
#   fetched once and shared between clones. Override with --bundle DIR.
#
# Options (env var or flag):
#   --hf-repo   REPO   Hugging Face dataset      (default: $HF_REPO or konkanello/trail_traces)
#   --bundle    DIR    where to download traces  (default: <artifact>/ae_bundle)
#   --bundle-mode MODE how to consume a pre-staged ../ae_bundle:
#                        reuse (default) read it in place, nothing is written
#                        link            symlink <artifact>/ae_bundle -> it
#                        copy            full byte-for-byte copy (needs the space)
#   --n         N      random traces to validate (default: 3)
#   --skip-deps        do not run install_deps.sh (deps already installed)
#   --skip-download    reuse an existing bundle (no HF download)
# ===========================================================================
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"                 # artifact root
SNIPER="$ROOT/simulator/sniper"
source "$HERE/lib/venv.sh"                        # put the AE Python venv on PATH (hf)

HF_REPO="${HF_REPO:-konkanello/trail_traces}"
BUNDLE="$ROOT/ae_bundle"
BUNDLE_SET=0                                      # did the user pass --bundle?
BUNDLE_MODE="${AE_BUNDLE_MODE:-reuse}"            # reuse | link | copy
NVAL=3
SKIP_DEPS=0
SKIP_DL=0
while [ $# -gt 0 ]; do case "$1" in
  --hf-repo) HF_REPO="$2"; shift 2;;
  --bundle) BUNDLE="$2"; BUNDLE_SET=1; shift 2;;
  --bundle-mode) BUNDLE_MODE="$2"; shift 2;;
  --n) NVAL="$2"; shift 2;;
  --skip-deps) SKIP_DEPS=1; shift;;
  --skip-download) SKIP_DL=1; shift;;
  # print only the leading header block, not every comment in the body
  -h|--help) awk 'NR>1 && !/^#/{exit} NR>1{sub(/^# ?/,""); print}' "$0"; exit 0;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done
case "$BUNDLE_MODE" in reuse|link|copy) ;; *)
  echo "unknown --bundle-mode: $BUNDLE_MODE (expected reuse, link or copy)"; exit 2;;
esac

C_G=$'\033[1;32m'; C_Y=$'\033[1;33m'; C_R=$'\033[1;31m'; C_0=$'\033[0m'
step() { echo; echo "${C_G}==== $* ====${C_0}"; }
die()  { echo "${C_R}ERROR: $*${C_0}" >&2; exit 1; }

echo "TRAIL artifact reproduction"
echo "  artifact root : $ROOT"
echo "  HF dataset    : $HF_REPO"
echo "  trace bundle  : $BUNDLE  (a pre-staged ../ae_bundle takes precedence — see [3/4])"

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
  # Pin the embedded-Python config to the OS python3-config so an active conda/pyenv
  # can't hijack the link (that would couple lib/sniper to the env at runtime).
  PYCFG=$(command -v /usr/bin/python3-config || command -v python3-config || echo python3-config)
  # Preflight: the final link needs the matching embedded-Python dev lib. Fail early
  # with the exact package instead of a cryptic `ld: cannot find -lpythonX.Y`.
  _cc=$(command -v gcc || command -v cc || echo gcc)
  _pyv=$("$PYCFG" --includes 2>/dev/null | grep -oE 'python3\.[0-9]+t?' | head -1 | sed 's/python//')
  if ! echo 'int main(void){return 0;}' | "$_cc" -xc - $("$PYCFG" --includes 2>/dev/null) \
        $("$PYCFG" --ldflags --embed 2>/dev/null) -o /tmp/_ae_pyembed 2>/dev/null; then
    rm -f /tmp/_ae_pyembed
    die "embedded-Python dev lib (-lpython${_pyv}) is missing — the build links it.
       Fix:  sudo apt-get install libpython${_pyv}-dev   (or python${_pyv}-dev),
       or re-run  bash experiments/ae/lib/install_deps.sh  which now installs it."
  fi
  rm -f /tmp/_ae_pyembed
  echo "  building (this takes a few minutes; no Pin/SDE/libtorch downloads; python3-config=$PYCFG) ..."
  ( cd "$SNIPER" && make -j"$(nproc)" SNIPER_TRACE_ONLY=1 PYTHON3_CONFIG="$PYCFG" replay ) || die "build failed."
  [ -x "$SNIPER/lib/sniper" ] || die "build finished but lib/sniper is missing."
fi
echo "  ${C_G}lib/sniper ready.${C_0}"

# --- [3/4] traces ------------------------------------------------------------
step "[3/4] Traces + trace-lists"

# A bundle staged one level above the artifact root is the pre-downloaded copy on
# a shared machine. Use it and skip the (hundreds of GB) download. setup_tlists.sh
# resolves __TRACE_ROOT__ to an ABSOLUTE path, so the bundle does not have to live
# inside the clone for anything downstream to work.
bundle_ok() {
  [ -d "$1/traces" ] && [ -d "$1/vm_tlist" ] && ls "$1"/vm_tlist/*.tlist >/dev/null 2>&1
}
PARENT_BUNDLE="$(cd "$ROOT/.." 2>/dev/null && pwd)/ae_bundle"

if [ "$SKIP_DL" -eq 0 ] && [ "$BUNDLE_SET" -eq 0 ] && \
   [ "$PARENT_BUNDLE" != "$BUNDLE" ] && bundle_ok "$PARENT_BUNDLE"; then
  echo "  ${C_G}found a pre-staged trace bundle:${C_0} $PARENT_BUNDLE"
  echo "  ($(find "$PARENT_BUNDLE/traces" -type f | wc -l) traces, $(ls "$PARENT_BUNDLE"/vm_tlist/*.tlist | wc -l) trace-lists) — the download is not needed."
  case "$BUNDLE_MODE" in
    reuse)
      echo "  --bundle-mode reuse: reading it in place (nothing is copied)."
      BUNDLE="$PARENT_BUNDLE"
      ;;
    link)
      if [ -e "$BUNDLE" ] && [ ! -L "$BUNDLE" ]; then
        die "$BUNDLE already exists and is not a symlink — remove it, or use --bundle-mode reuse."
      fi
      ln -sfn "$PARENT_BUNDLE" "$BUNDLE" || die "could not symlink $BUNDLE -> $PARENT_BUNDLE"
      echo "  --bundle-mode link: $BUNDLE -> $PARENT_BUNDLE"
      ;;
    copy)
      echo "  ${C_Y}--bundle-mode copy: copying the whole bundle into $BUNDLE.${C_0}"
      echo "  ${C_Y}This duplicates the full trace set on disk and can take a long time.${C_0}"
      mkdir -p "$BUNDLE"
      if command -v rsync >/dev/null 2>&1; then
        rsync -a --info=progress2 "$PARENT_BUNDLE"/ "$BUNDLE"/ || die "copying the bundle failed."
      else
        cp -a "$PARENT_BUNDLE"/. "$BUNDLE"/ || die "copying the bundle failed."
      fi
      ;;
  esac
  SKIP_DL=1
fi

mkdir -p "$BUNDLE"
if [ "$SKIP_DL" -eq 1 ]; then
  echo "  using trace bundle: $BUNDLE  (no download)"
else
  command -v hf >/dev/null 2>&1 || {
    echo "  hf not found — creating the local venv and installing huggingface_hub ..."
    python3 -m venv "$AE_VENV" 2>/dev/null || true
    "$AE_VENV/bin/pip" install -q -U huggingface_hub \
      || die "could not install huggingface_hub — run experiments/ae/lib/install_deps.sh first."
    export PATH="$AE_VENV/bin:$PATH"
  }
  echo "  downloading dataset '$HF_REPO' -> $BUNDLE"
  echo "  ${C_Y}(this is large — the full trace set; the download is resumable, so you can re-run build_and_validate.sh if it is interrupted)${C_0}"
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

Reproduce the paper results with ONE driver — ae_run_all.sh. It launches every
suite, tracks them in the background, and writes the tables/figures to
experiments/ae/ae_out/. You do NOT run anything per-suite.

  On a SLURM cluster (all suites in parallel):
    bash experiments/ae/ae_run_all.sh --mode slurm [--partitions <p>]   # launch
    bash experiments/ae/ae_run_all.sh --status                          # progress, any time
    bash experiments/ae/ae_run_all.sh --results                         # once every suite reads DONE

  On a single machine (no SLURM; one shared scheduler across your cores):
    bash experiments/ae/ae_run_all.sh --mode local --jobs \$(nproc)      # launch
    bash experiments/ae/ae_run_all.sh --status
    bash experiments/ae/ae_run_all.sh --results

  Subset:  --suites "head8mb multicore"   |   quick test:  --icount 2000000
  Suites:  head8mb head2mb table5 table6 pqsweep multicore

Motivation figures (Figures 4, 5, 6, 8, 9 — separate, no simulation):

    bash experiments/ae/motivation/run_motivation.sh --mode local --jobs \$(nproc)   # 1. analyse
    bash experiments/ae/motivation/run_motivation.sh --plot                          # 2. figures
    #   on a cluster instead:  --mode slurm [--partitions <p>]  — step 1 only submits
    #   the jobs there, so run step 2 once they finish (it refuses to draw partial
    #   figures and tells you how many of the workloads are ready).
    # Step 1 reuses a pre-staged bundle (\$HOME/ptw_bundle, ../ptw_bundle, ./ptw_bundle)
    # and tells you so. Only if there is none do you need the 2.9 GB download first:
    #   bash experiments/ae/motivation/run_motivation.sh --download

Don't run launch/status/results back-to-back: launch once, poll --status until
every suite reads DONE, then --results. Full details: experiments/ae/README.md.
NEXT
