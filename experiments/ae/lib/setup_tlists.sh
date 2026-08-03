#!/bin/bash
# setup_tlists.sh  TRACES_DIR  DEST_DIR  [SRC_TLIST_DIR]
#
# Resolve the placeholder trace-lists (which use __TRACE_ROOT__ in place of the
# traces directory) to your downloaded traces, writing the resolved *.tlist into
# DEST_DIR (point this at the repo's experiments/vm_tlist).
#
#   TRACES_DIR     : the downloaded flat traces/ folder (the *.champsim.gz /
#                    *.sift / *.champsimtrace.xz files).
#   DEST_DIR       : where to write the resolved trace-lists.
#   SRC_TLIST_DIR  : dir holding the placeholder *.tlist (default: this script's
#                    own dir, i.e. the bundle's vm_tlist/).
set -uo pipefail
TR="${1:?usage: setup_tlists.sh <traces_dir> <dest_dir> [src_tlist_dir]}"
DEST="${2:?usage: setup_tlists.sh <traces_dir> <dest_dir> [src_tlist_dir]}"
SRC="${3:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"

[ -d "$TR" ]  || { echo "error: traces dir '$TR' not found" >&2; exit 1; }
[ -d "$SRC" ] || { echo "error: source trace-list dir '$SRC' not found" >&2; exit 1; }
ls "$SRC"/*.tlist >/dev/null 2>&1 || { echo "error: no *.tlist in $SRC" >&2; exit 1; }
TR="$(cd "$TR" && pwd)"
mkdir -p "$DEST"

n=0
for f in "$SRC"/*.tlist; do
  sed "s#__TRACE_ROOT__#${TR}#g" "$f" > "$DEST/$(basename "$f")"
  n=$((n+1))
done
echo "resolved $n trace-lists -> $DEST   (traces: $TR)"

# --- sanity: check the first DATA line (has a comma) of a known list resolves ---
probe=$(grep ',' "$DEST/top250_virtuoso.tlist" 2>/dev/null | grep -vE '^\s*#' | head -1 \
        | awk -F',' '{gsub(/^[ \t]+|[ \t]+$/,"",$2); print $2}')
if [ -n "${probe:-}" ]; then
  if [ -f "$TR/$probe" ]; then
    echo "sanity ok: $TR/$probe exists"
  else
    echo "WARNING: sample trace '$TR/$probe' not found — is TRACES_DIR the flat traces/ folder?" >&2
  fi
fi
exit 0
