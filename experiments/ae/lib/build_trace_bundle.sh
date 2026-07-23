#!/bin/bash
# build_trace_bundle.sh DEST TRACE_LIST [--instr N] [--jobs J] [--level L]
#
# Assembles the AE trace bundle under DEST, mirroring each trace's path below
# /mnt/ (so /mnt/galactica/... -> DEST/galactica/...).  Google `gtrace`
# champsim traces are TRUNCATED to the first N instructions (default 500M) at
# exact 64-byte record boundaries; every other trace is copied as-is.
#
# ChampSim trace = pure stream of fixed 64-byte `input_instr` records (no
# header; the reader reads sizeof(input_instr) until EOF). So N instructions =
# the first N*64 uncompressed bytes.  Truncation is verified INLINE: the exact
# number of uncompressed bytes kept is counted (tee|wc) and divided by 64 —
# if it equals N, the truncated trace has exactly N instructions.
#
# Resumable (skips traces already present in DEST) and parallel (--jobs).
set -uo pipefail
DEST="${1:?usage: build_trace_bundle.sh DEST TRACE_LIST [--instr N] [--jobs J] [--level L]}"
LIST="${2:?need a trace-list file (one absolute trace path per line)}"; shift 2
INSTR=500000000; JOBS=$(( $(nproc) - 2 )); LEVEL=6
while [ $# -gt 0 ]; do case "$1" in
  --instr) INSTR="$2"; shift 2;; --jobs) JOBS="$2"; shift 2;; --level) LEVEL="$2"; shift 2;;
  *) echo "unknown arg: $1"; exit 2;; esac; done
RECSIZE=64; KEEP=$(( RECSIZE * INSTR ))
export DEST RECSIZE KEEP INSTR LEVEL

process_one() {
  local src="$1"
  [ -e "$src" ] || { echo "MISSING src: $src"; return; }
  local dst="$DEST/${src#/mnt/}"
  mkdir -p "$(dirname "$dst")"
  if [ -s "$dst" ]; then echo "skip(exists) ${src##*/}"; return; fi
  case "$src" in
    */google-dpc4/*.champsim.gz)
      # keep first KEEP uncompressed bytes; count them inline for verification
      local kept
      kept=$(zcat "$src" 2>/dev/null | head -c "$KEEP" \
             | tee >(gzip -"$LEVEL" > "$dst.tmp") | wc -c)
      mv "$dst.tmp" "$dst"
      local ninstr=$(( kept / RECSIZE ))
      if [ "$kept" -eq "$KEEP" ]; then
        echo "trunc OK  ${src##*/}: ${ninstr} instr ($(du -h "$dst" 2>/dev/null|cut -f1))"
      else
        echo "trunc SHORT ${src##*/}: only ${ninstr} instr (<${INSTR}); kept whole trace ($(du -h "$dst" 2>/dev/null|cut -f1))"
      fi
      ;;
    *)
      cp "$src" "$dst.tmp" && mv "$dst.tmp" "$dst" && echo "copy      ${src##*/}"
      ;;
  esac
}
export -f process_one

n=$(grep -c . "$LIST")
echo "[bundle] $n traces -> $DEST  (google truncated to ${INSTR} instr, gzip -${LEVEL}, ${JOBS}-way)"
grep . "$LIST" | xargs -P "$JOBS" -I{} bash -c 'process_one "$@"' _ {}
echo "[bundle] done. Size: $(du -sh "$DEST" 2>/dev/null | cut -f1)"
