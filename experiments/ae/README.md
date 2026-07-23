# TRAIL — Artifact Evaluation Harness

One command builds the simulator, runs an experiment, and emits the paper's
table/figure — in **SLURM** or **local** mode (reviewer's choice).

```
experiments/ae/run_ae.sh --mode {slurm|local} --claim {CLAIM|all} \
                         [--install-deps] [--build] [--jobs N] [--out DIR] [--dry-run]
```

## Claims (map to the paper)

| `--claim`   | reproduces                                   | jobs  |
|-------------|----------------------------------------------|------:|
| `head8mb`   | Head-to-head @ 8MB NUCA (main result)        | 2761  |
| `head2mb`   | Head-to-head @ 2MB NUCA                       | 2761  |
| `table5`    | Table 5 — in-PTE payload budget sweep        | 5271  |
| `table6`    | Table 6 — side-car (Trail-External) sweep    | 6275  |
| `pqsweep`   | L2-TLB PQ-size sensitivity                    | 3765  |
| `multicore` | 4-core, 100-mix head-to-head                 | 1000  |
| `all`       | everything above                             | ~21.8k|

Each single-core claim carries its own **ASP baseline**; the multicore claim
carries its own **no-prefetch baseline**. Speedups are geomeans over the
curated top-200 (`top200_trail_workloads.txt`); multicore uses the equal-work
50M-per-core heartbeat-crossing method.

## Traces & trace lists
The ChampSim traces are large and are distributed **separately** (with the
trace bundle), not in this repo. Each experiment suite is driven by a **trace
list** (`*.tlist`) whose **first line is the absolute path to your trace root**
and whose remaining lines are `tracename, filename, ...`.

The `.tlist` files ship **with the trace bundle** (their paths already match the
bundle layout). After downloading the traces:

1. Copy the bundle's `.tlist` files into **`experiments/vm_tlist/`** (this
   directory is git-ignored — it is populated from the bundle, not the repo).
2. If your traces live somewhere else, edit the **first line** of each `.tlist`
   to point at your trace root.

`run_ae.sh` reads the `.tlist` files named by `clist_prefetcher_v3.yaml`
(single-core) and `clist_multicore.yaml` (multicore); a suite fails fast with a
clear message if its trace list or any referenced trace is missing.

## Requirements
- **Traces + trace lists** installed as above (see *Traces & trace lists*).
- **System packages** for the build — installed by `lib/install_deps.sh`
  (Debian/Ubuntu). This is the **trace-replay** build: it needs **no Intel
  Pin/SDE** (those are only for live-binary instrumentation); the instruction
  decoder `libxed` is bundled with the artifact. Deps are just the toolchain +
  `libboost-dev libsqlite3-dev zlib1g-dev libbz2-dev liblzma-dev
  libexpat1-dev python3-dev`.
- `--mode slurm` needs `sbatch`/`squeue`; `--mode local` needs none.

## Quick start (local mode, from source)
```
# 1) install build deps (once; needs sudo), 2) build+validate, 3) run+parse:
sudo experiments/ae/lib/install_deps.sh
cd experiments/ae
./run_ae.sh --build --mode local --claim table5 --jobs 16
```
Or fold the install into one command:
```
./run_ae.sh --install-deps --build --mode local --claim table5 --jobs 16
```
`--build` compiles `lib/sniper` and runs a **post-build smoke test** (a short
single-core sim that must yield a valid IPC) before any experiment launches —
if the build or smoke sim fails, the run aborts.

## Usage
```
# SLURM cluster (submits all jobs, waits, parses):
./run_ae.sh --build --mode slurm --claim all

# single big machine (runs N sims at a time, blocks, parses):
./run_ae.sh --build --mode local --claim head8mb --jobs 32

# see what would run without launching:
./run_ae.sh --mode local --claim all --dry-run
```

## Output
Markdown tables land in `--out` (default `experiments/ae/ae_out/`), one per
claim, each showing **ours (paper)** so the match is visible at a glance.

## Layout
```
ae/
  run_ae.sh                 # orchestrator (generate -> launch -> wait -> parse)
  lib/
    install_deps.sh         # apt install build deps (no Pin/SDE needed)
    build_and_validate.sh   # make + post-build smoke sim (mandatory --build)
    launch.sh               # slurm submit  |  local xargs -P run
    jobfile_to_cmds.py      # strip sbatch wrapper -> bare run-sniper commands
  parse/
    parse_headtohead.py     # head8mb / head2mb
    parse_table5.py         # Table 5
    parse_table6.py         # Table 6
    parse_pqsweep.py        # PQ sweep
    parse_multicore.py      # 4-core equal-work (50M heartbeat crossings)
```

## Notes
- `local` mode strips the SLURM wrapper and runs the identical `run-sniper`
  commands directly, so both modes exercise the same binary and configs.
- All experiments run on the **corrected cost model** binary (prefetch-fault
  walks allocate the page *and* pay their walk latency).
