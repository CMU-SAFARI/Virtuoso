#!/bin/bash
# upload_traces.sh — upload the AE traces to a Google Cloud Storage bucket.
#
#   upload_traces.sh gs://BUCKET/prefix [--manifest FILE] [--dry-run]
#
# Traces are ALREADY compressed (.champsim.gz / .champsimtrace.xz / .sift),
# so we do NOT re-zip — we upload them as-is with `gsutil -m cp` (parallel,
# resumable). Directory structure under each trace root is preserved so the
# trace_list paths can be rewritten with a single root swap.
#
# Requires: gcloud SDK + gsutil, authenticated (`gcloud auth login`), a bucket
# you own, and enough egress. Footprint is LARGE (~574 GB for the full set).
#
# Build the manifest first (unique traces across all AE claims):
#   cat experiments/exp_ae_*/trace_list.csv experiments/exp_table*/trace_list.csv \
#     | grep -oE '/mnt/[^,]+\.(champsim\.gz|champsimtrace\.xz|sift)' | sort -u > traces.manifest
set -euo pipefail
DEST="${1:?usage: upload_traces.sh gs://BUCKET/prefix [--manifest FILE] [--dry-run]}"; shift || true
MANIFEST="traces.manifest"; DRY=0
while [ $# -gt 0 ]; do case "$1" in
  --manifest) MANIFEST="$2"; shift 2;;
  --dry-run) DRY=1; shift;;
  *) echo "unknown arg: $1"; exit 2;;
esac; done

command -v gsutil >/dev/null || { echo "gsutil not found — install the gcloud SDK and run 'gcloud auth login'."; exit 1; }
[ -f "$MANIFEST" ] || { echo "manifest '$MANIFEST' not found (see header for how to build it)."; exit 1; }

n=$(wc -l < "$MANIFEST")
tot=$(du -ch $(cat "$MANIFEST") 2>/dev/null | tail -1 | awk '{print $1}')
echo "Uploading $n traces (~$tot) to $DEST ..."
[ "$DRY" -eq 1 ] && { echo "[dry-run] would gsutil -m cp -n each trace preserving path under its root"; exit 0; }

# Upload each trace, preserving its absolute path under the destination prefix
# (strip the leading '/', so /mnt/galactica/... -> $DEST/mnt/galactica/...).
# -n = no-clobber (resumable across reruns); -m = parallel.
while IFS= read -r t; do
  [ -e "$t" ] || { echo "  SKIP missing: $t"; continue; }
  rel="${t#/}"
  gsutil -m cp -n "$t" "$DEST/$rel"
done < "$MANIFEST"
echo "Done. Reviewers set the trace root to $DEST and the trace_list paths resolve unchanged."
