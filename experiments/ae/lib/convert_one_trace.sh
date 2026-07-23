#!/bin/bash
# convert_one_trace.sh SRC [DEST] [INSTR]
# One trace -> DEST (mirroring path below /mnt/). Google gtrace champsim traces
# are truncated to INSTR (default 500M) instructions at 64-byte record
# boundaries (verified inline); everything else is copied as-is. Resumable.
set -uo pipefail
SRC="${1:?need SRC trace path}"
DEST="${2:-./ae_traces}"
INSTR="${3:-500000000}"
RECSIZE=64; KEEP=$(( RECSIZE * INSTR )); LEVEL=6
[ -e "$SRC" ] || { echo "MISSING src: $SRC"; exit 1; }
dst="$DEST/${SRC#/mnt/}"; mkdir -p "$(dirname "$dst")"
if [ -s "$dst" ]; then echo "skip(exists) ${SRC##*/}"; exit 0; fi
case "$SRC" in
  */google-dpc4/*.champsim.gz)
    kept=$(zcat "$SRC" 2>/dev/null | head -c "$KEEP" | tee >(gzip -"$LEVEL" > "$dst.tmp") | wc -c)
    mv "$dst.tmp" "$dst"
    n=$(( kept / RECSIZE ))
    [ "$kept" -eq "$KEEP" ] && echo "trunc OK ${SRC##*/}: $n instr ($(du -h "$dst"|cut -f1))" \
                            || echo "trunc SHORT ${SRC##*/}: $n instr (<$INSTR); kept whole"
    ;;
  *)
    cp "$SRC" "$dst.tmp" && mv "$dst.tmp" "$dst" && echo "copy ${SRC##*/}"
    ;;
esac
