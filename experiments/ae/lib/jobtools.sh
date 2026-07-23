#!/bin/bash
# jobtools.sh — Tier-1 reliability helpers for the AE harness.
#   - validity-based completion (a job is "done" only if sim.stats has a cycle_count)
#   - pre-flight trace check (fail fast before submitting)
#   - resumable / self-healing submit (only submit jobs with no valid result)
# Sourced by run_ae.sh. All functions are pure bash + coreutils + slurm.

# --- a result is VALID only if sim.stats exists and carries the IPC counters ---
ae_valid_result() {  # $1 = rundir (the -d dir)
  local s="$1/simulation/sim.stats"
  [ -s "$s" ] && grep -q '^performance_model.cycle_count =' "$s" 2>/dev/null
}

# --- ensure a sbatch line targets the requested partitions (pure bash) ---
ae_ensure_partition() {  # $1=line $2=partitions(csv)
  local line="$1" parts="$2" val
  case "$line" in
    *--partition=*)
      val="${line#*--partition=}"; val="${val%% *}"
      printf '%s' "${line/--partition=$val/--partition=$parts}" ;;
    *)
      printf '%s' "${line/sbatch/sbatch --partition=$parts}" ;;
  esac
}

# --- extract the -d rundir from a line (pure bash, no subshell) ---
ae_line_rundir() { local t="${1#* -d }"; printf '%s' "${t%% *}"; }

# --- PRE-FLIGHT: stat every unique trace referenced by the jobfile ---------
# Returns 0 if all present; prints the missing ones and returns 1 otherwise.
ae_preflight_traces() {  # $1 = jobfile
  local jf="$1" missing=0 total
  # traces appear as --traces=a,b,c ; split on comma
  local tmp; tmp=$(mktemp)
  grep -oE -- '--traces=[^ ]+' "$jf" | sed 's/--traces=//' | tr ',' '\n' | sort -u > "$tmp"
  total=$(wc -l < "$tmp")
  echo "[preflight] checking $total unique traces ..."
  while IFS= read -r t; do
    [ -z "$t" ] && continue
    [ -e "$t" ] || { echo "  MISSING: $t"; missing=$((missing+1)); }
  done < "$tmp"
  rm -f "$tmp"
  if [ "$missing" -gt 0 ]; then
    echo "[preflight] $missing/$total traces UNREACHABLE — fix the mount/paths and retry (or pass --no-preflight)."
    return 1
  fi
  echo "[preflight] all $total traces reachable."
  return 0
}

# --- emit sbatch lines whose result is NOT valid (missing/failed/truncated) --
# Fast: build the set of valid run-dir basenames once via `find -size` (a valid
# sim.stats is ~140KB; missing/failed jobs have none or a tiny one), then filter
# the jobfile with bash builtins. Injects partitions. NUL-separated to stdout.
ae_missing_lines() {  # $1=jobfile $2=partitions
  local jf="$1" parts="$2" rroot d b line
  rroot=$(grep -m1 -oE -- '-d [^ ]+/results' "$jf" | awk '{print $2}')
  declare -A VALID
  while IFS= read -r p; do VALID["${p##*/}"]=1; done < <(
    find "$rroot" -maxdepth 3 -name sim.stats -size +50k -printf '%h\n' 2>/dev/null | sed -E 's#/simulation$##')
  while IFS= read -r line; do
    case "$line" in sbatch*) ;; *) continue;; esac
    d="${line#* -d }"; d="${d%% *}"; b="${d##*/}"
    [ -n "${VALID[$b]:-}" ] || printf '%s\0' "$(ae_ensure_partition "$line" "$parts")"
  done < "$jf"
}

# --- count valid results under a results dir (fast: file presence during poll) ---
ae_count_valid() {  # $1=results_dir
  # cheap proxy during polling: sim.stats file count (validity re-checked at heal)
  find "$1" -maxdepth 3 -name sim.stats 2>/dev/null | wc -l
}
