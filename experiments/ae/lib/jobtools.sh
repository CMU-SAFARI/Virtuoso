#!/bin/bash
# jobtools.sh — Tier-1 reliability helpers for the AE harness.
#   - validity-based completion (a job is "done" only if sim.stats has a cycle_count)
#   - pre-flight trace check (fail fast before submitting)
#   - resumable / self-healing submit (only submit jobs with no valid result)
# Sourced by ae_launch.sh / ae_watch.sh. All functions are pure bash + coreutils + slurm.

# --- a result is VALID only if sim.stats exists and carries the IPC counters ---
ae_valid_result() {  # $1 = rundir (the -d dir)
  local s="$1/simulation/sim.stats"
  [ -s "$s" ] && grep -q '^performance_model.cycle_count =' "$s" 2>/dev/null
}

# The motivation suite's equivalent: one JSON per workload. Checked for content,
# not just existence — analyze_dump.py writes atomically, but a JSON left by an
# older version (or copied in by hand) could still be truncated, and both the
# watcher and the resume logic would then treat that workload as finished.
ae_valid_json() {  # $1 = <workload>.json
  [ -s "$1" ] && tail -c 2 "$1" 2>/dev/null | tr -d '\n' | grep -q '}$'
}

# --- ensure a sbatch line targets the requested partitions (pure bash) ---
ae_ensure_partition() {  # $1=line $2=partitions(csv, empty = leave as-is)
  local line="$1" parts="$2" val
  [ -z "$parts" ] && { printf '%s' "$line"; return; }   # no partition requested -> don't inject one
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
  # traces appear as --traces=a,b,c ; split on comma.
  # The multicore generator puts --traces LAST inside a double-quoted command,
  # so the closing quote is glued to the final path (…/d.champsim.gz"). Strip
  # quotes before stat-ing, or every mix's last trace is reported MISSING.
  local tmp; tmp=$(mktemp)
  grep -oE -- '--traces=[^ ]+' "$jf" | sed 's/--traces=//' | tr ',' '\n' \
    | tr -d '"'"'" | sort -u > "$tmp"
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

# Fills VALID / HAVEDB (declared by the caller; bash scoping is dynamic) with
# the run dirs under $1 that have a valid sim.stats / a sim.stats.sqlite3.
# Keyed by the full run dir, spelled as in the jobfile's -d.  Single-core run
# dirs are results/<rundir>, multicore results/<suite>/<rundir>, so the
# files are at depth 3 or 4.
_ae_scan_results() {  # $1 = results root
  local p
  VALID=(); HAVEDB=()
  while IFS= read -r p; do VALID["${p%/simulation}"]=1; done < <(
    find "$1" -maxdepth 4 -path '*/simulation/sim.stats' -size +50k -printf '%h\n' 2>/dev/null)
  while IFS= read -r p; do HAVEDB["${p%/simulation}"]=1; done < <(
    find "$1" -maxdepth 4 -path '*/simulation/sim.stats.sqlite3' -printf '%h\n' 2>/dev/null)
}

# --- emit sbatch lines whose result is NOT valid (missing/failed/truncated) --
# Fast: build the set of valid run dirs once via `find -size` (a valid sim.stats
# is ~140KB; missing/failed jobs have none or a tiny one), then filter the
# jobfile with bash builtins. Injects partitions. NUL-separated to stdout.
ae_missing_lines() {  # $1=jobfile $2=partitions
  # Emit (NUL-separated, partitions injected) the sbatch lines that still need
  # to run.  A line is skipped when its run dir
  #   - already has a valid sim.stats (after regenerating it where possible), or
  #   - belongs to a job that is queued or running right now.
  # The second rule prevents duplicate runs: two runs in one directory corrupt
  # each other's statistics (run-sniper also refuses a second run into a
  # directory it is using).
  # Returns 1, emitting nothing, if the queue cannot be read.
  local jf="$1" parts="$2" rroot d line p q errf cand nq=0
  declare -A VALID HAVEDB QUEUED
  # 1. Queue snapshot FIRST, so a job that finishes while the scans below run
  #    is still seen as queued rather than resubmitted.  Its run dir comes from
  #    --output=<rundir>/slurm.out.  If squeue fails, stop: carrying on with
  #    an empty list would resubmit every in-flight job.
  errf=$(mktemp)
  if ! q=$(squeue -u "$USER" -h -O "STDOUT:1024" 2>"$errf"); then
    echo "  ERROR: squeue failed; cannot tell which jobs are already queued/running:" >&2
    sed 's/^/    /' "$errf" >&2
    echo "  Nothing submitted (it would duplicate in-flight jobs). Retry when squeue works." >&2
    rm -f "$errf"; return 1
  fi
  rm -f "$errf"
  # squeue pads each field to 1024 chars; `read` with the default IFS strips
  # the padding (a parameter-expansion trim is quadratic: ~0.1 s per job).
  while read -r p; do
    [ -n "$p" ] && QUEUED["${p%/*}"]=1
  done <<< "$q"
  # 2. Results on disk.
  rroot=$(grep -m1 -oE -- '-d [^ ]+/results' "$jf" | awk '{print $2}')
  _ae_scan_results "$rroot"
  # 3. Finished runs (stats DB present) whose sim.stats is missing/invalid:
  #    rebuild it, never re-run.  Run dirs of in-flight jobs are left alone.
  cand=$(mktemp)
  while IFS= read -r line; do
    case "$line" in sbatch*) ;; *) continue;; esac
    d="${line#* -d }"; d="${d%% *}"
    [ -n "${HAVEDB[$d]:-}" ] && [ -z "${VALID[$d]:-}" ] && [ -z "${QUEUED[$d]:-}" ] && printf '%s\n' "$d"
  done < "$jf" > "$cand"
  if [ -s "$cand" ]; then
    xargs -a "$cand" -d '\n' python3 "$AE_LIB/regen_stats.py" "$ROOT/simulator/sniper" | awk '
      $1=="REGENERATED"{r++} $1=="SKIP"{s[$2]++}
      END{ if (r) printf "  regenerated sim.stats for %d finished run(s)\n", r > "/dev/stderr";
           for (k in s) if (k!="valid") printf "  not regenerated (%s): %d\n", k, s[k] > "/dev/stderr" }'
    _ae_scan_results "$rroot"
  fi
  rm -f "$cand"
  # 4. Emit what is neither valid nor in flight.
  while IFS= read -r line; do
    case "$line" in sbatch*) ;; *) continue;; esac
    d="${line#* -d }"; d="${d%% *}"
    [ -n "${VALID[$d]:-}" ] && continue
    if [ -n "${QUEUED[$d]:-}" ]; then nq=$((nq+1)); continue; fi
    printf '%s\0' "$(ae_ensure_partition "$line" "$parts")"
  done < "$jf"
  [ "$nq" -gt 0 ] && echo "  skipped $nq job(s) already queued/running" >&2
  return 0
}

# --- count valid results under a results dir (fast: file presence during poll) ---
ae_count_valid() {  # $1=results_dir
  # cheap proxy during polling: sim.stats file count (validity re-checked at heal)
  find "$1" -maxdepth 3 -name sim.stats 2>/dev/null | wc -l
}
