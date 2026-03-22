#!/usr/bin/env bash
# Merge every *.vma under ../virtuoso_traces into <workload>.merged

set -euo pipefail

for vma in ../../../../virtuoso_traces/*.vma; do
    python3 merge_vmas.py "$vma"
    mv "$vma.merged" "${vma%.vma}.merged"
done
