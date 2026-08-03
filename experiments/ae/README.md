# Revelator — Artifact Evaluation

Revelator is a hash-based speculative address translation design: a hash-based
physical memory allocator in MimicOS places each page where a hash of its virtual
address says it should go, and a speculation engine in the MMU replays that hash
to guess the physical frame while the page walk proceeds in parallel. Because the
guess is only correct while the allocator can still honour the hash, the design is
a co-design — and how it behaves as memory fills up is as much a result as the
headline speedup.

This harness runs Revelator and Revelator-THP against the no-speculation baseline
and the prior speculative designs shipped in Virtuoso, on the Sniper architectural
simulator with the MimicOS operating-system model.

Reproducing a result takes three steps: **(1) install dependencies → (2) build &
validate → (3) run the experiments.**

> **Status.** The harness, the parsers and the plotters are exercised end-to-end
> against synthetic results, and every config file the suites reference exists in
> the tree. The suites themselves have **not** been run to completion on real
> traces in this branch, so the "Expected results" section below gives the shape
> of the result, not calibrated numbers. Run `utilsweep` first if you want the
> quickest meaningful signal.

---

## Requirements

**Software**
- Linux (Debian/Ubuntu recommended; tested on Ubuntu 20.04, 22.04 and 24.04).
- A C++17 toolchain and Python 3.8+. All packages — plus the Hugging Face CLI
  (trace download) and matplotlib (figures) — are installed by
  `experiments/ae/lib/install_deps.sh`. No license-gated or proprietary tools are
  required; the instruction decoder is fetched and built automatically.
- Network access (to download the traces and fetch the decoder during the build).
- *Optional:* a SLURM cluster. Without SLURM everything still runs locally.

**Hardware**
- **Disk:** ~300 GB free (≈250 GB of traces, plus the build and results).
- **RAM:** 8 GB is enough for the build and the sanity check. Each simulation
  uses a few GB, so budget more if you run many in parallel.
- **CPU:** any x86-64 machine. More cores (or a cluster) only reduce wall time.

---

## Step 1 — Install dependencies

```bash
git clone --branch revelator-artifact-release https://github.com/CMU-SAFARI/Virtuoso.git
cd Virtuoso
bash experiments/ae/lib/install_deps.sh        # uses sudo if you are not root
```

---

## Step 2 — Build & validate

```bash
bash experiments/ae/build_and_validate.sh --skip-deps
```

Builds the simulator, downloads the trace dataset, and runs a few short
simulations on randomly chosen traces (using `revelator.cfg`) to confirm the whole
pipeline works. It shows progress and then **stops**:

```
[1/4] system build dependencies       (skipped — done in Step 1)
[2/4] build the simulator             (~2-3 min)
[3/4] download traces + trace-lists    (public dataset; ~250 GB; resumable)
[4/4] validate on 3 random traces      ->  [PASS] <trace> IPC=...
      "Setup is validated. You are ready to run the experiments."
```

Every phase is resumable — re-run the command after an interruption and it skips
whatever is already done.

Useful flags: `--n N` (number of validation traces), `--skip-download` (reuse an
existing download), `--bundle DIR` (where to download). The traces are the public
dataset
[`konkanello/trail_traces`](https://huggingface.co/datasets/konkanello/trail_traces)
(`traces/` + `vm_tlist/`) — despite the name this is the shared Virtuoso trace
bundle, not a TRAIL-specific one. `build_and_validate.sh` downloads it and wires
the trace-lists into `experiments/vm_tlist/` for you.

---

## Step 3 — Run the experiments

| suite           | what it measures                                                        | configs | output |
|-----------------|-------------------------------------------------------------------------|--------:|--------|
| `revelator`     | Revelator (4KB) vs ReserveTHP floor, SpOT, SpecTLB, ASAP, and the ceilings |       9 | `revelator.pdf` |
| `revelator_thp` | Revelator-THP vs the floor, ASAP, 4KB Revelator, and the ceilings          |       7 | `revelator_thp.pdf` |
| `utilsweep`     | both variants as memory fills (0% → 80% occupied)                          |    16\* | `utilsweep.pdf` |
| `multicore`     | 4-core Revelator vs the ReserveTHP baseline, top-250 mixes                 |       2 | `multicore.pdf` |

\* one baseline + three variants × five occupancy points.

Job count is `configs × traces` (single-core, 300 M instructions each) and
`configs × mixes` (multicore, 50 M/core). The trace suite is `top250`, so expect
a few thousand single-core jobs per suite.

`ae_run_all.sh` is the **one command you drive**, in three passes: **launch** once
(non-blocking — the watchers run in the background), poll **`--status`** until
every suite reads `DONE`, then run **`--results`**. Don't run the three
back-to-back; `--results` only works once the jobs finish.

### A) On a SLURM cluster

```bash
bash experiments/ae/ae_run_all.sh --mode slurm --partitions <partition>   # launch
bash experiments/ae/ae_run_all.sh --status                                 # progress, any time
bash experiments/ae/ae_run_all.sh --results                                # once every suite reads DONE
```

Suites are independent and proceed in parallel. `--partitions` is optional (omit
it to use your cluster's default). Restrict the set with
`--suites "revelator utilsweep"`.

### B) On a single machine (no SLURM)

Launch as many suites as you like: a single shared scheduler runs their
simulations through this machine's cores, keeping at most `--jobs` (default:
cores − 2) running at a time.

```bash
bash experiments/ae/ae_run_all.sh --mode local --jobs $(nproc)   # launch all suites
bash experiments/ae/ae_run_all.sh --status
bash experiments/ae/ae_run_all.sh --results
```

A full 300 M-instruction suite is large on one machine — add `--icount 2000000`
to shrink every job to a minutes-long end-to-end smoke test (the numbers will not
be meaningful at 2 M instructions, but the pipeline is exercised). `--icount`
works in SLURM mode too, and covers the multicore suite's per-core budget.

### What you get

`--results` writes, per suite, `ae_out/<suite>.md` (the table), `ae_out/<figure>.pdf`
(the chart) and `ae_out/<figure>_table.pdf` (the same table rendered as an image).
Progress lives in `ae_out/<suite>.status`, and each finished suite gets an
`ae_out/<suite>.DONE` pass/fail report listing any failed jobs and where their logs
are.

### If something goes wrong — manual per-suite control

**You normally never run these.** Reach for them only to recover a suite that
reported failures:

```bash
bash experiments/ae/ae_launch.sh  --suite revelator --mode slurm --partitions <partition>
#                                  single machine instead:  --mode local --jobs $(nproc)
bash experiments/ae/ae_watch.sh   --suite revelator
bash experiments/ae/ae_results.sh --suite revelator --wait
```

Re-running `ae_launch.sh` only resubmits jobs that do not yet have a valid result,
so it cleanly heals a partially-failed suite without repeating finished work.

---

## Expected results

The shape to look for, in each suite:

| suite | what should hold |
|-------|------------------|
| `revelator` | Revelator beats the ReserveTHP floor and the prior speculative designs (SpOT, SpecTLB, ASAP). 3 hashes ≥ 1 hash. Both stay **below** `Revelator (oracle)`, which stays below `Oracle spec.` and `No translation` — those three are upper bounds, and a design beating its own oracle means a broken run, not a good result. |
| `revelator_thp` | Revelator-THP beats both the floor and 4KB Revelator, since huge pages cut the number of translations the hash has to get right. Same ceilings apply. |
| `utilsweep` | The win is largest at 0% occupancy and decays monotonically as memory fills — that decay *is* the result. More hashes should decay more gracefully, since the allocator has more placements to choose from. |
| `multicore` | Revelator's 4-core speedup should be at least its single-core one; translation pressure grows with core count. STP, aggIPC and HM-IPC should agree to within a point or two — a large gap between them means the mixes are badly unbalanced. |

The multicore parser uses the **equal-work per-core heartbeat** method rather than
`sim.stats` global `cycle_count`, which overshoots by however far the last core ran
past the stop point and systematically flatters whichever config finishes its cores
most unevenly. `--target auto` picks the largest instruction count every run in the
suite actually reached, so a short `--icount` run still parses.

---

## Layout

```
experiments/ae/
├── build_and_validate.sh     Step 2: deps -> build -> traces -> sanity check
├── ae_run_all.sh             Step 3: the one driver (launch / --status / --results)
├── ae_launch.sh              per-suite: generate jobfile + submit  (recovery only)
├── ae_watch.sh               per-suite: background progress watcher (recovery only)
├── ae_results.sh             per-suite: parse + plot               (recovery only)
├── lib/
│   ├── ae_common.sh          suite registry: suite -> yaml suite, parser, figure
│   ├── jobtools.sh           validity-based completion, preflight, resumable submit
│   ├── install_deps.sh       apt packages + the AE Python venv
│   ├── setup_tlists.sh       resolve __TRACE_ROOT__ in the downloaded trace-lists
│   ├── validate_traces.sh    short sims on random traces (uses revelator.cfg)
│   └── …
├── parse/
│   ├── parse_revelator.py    head-to-head (--variant base|thp)
│   ├── parse_utilsweep.py    speedup per memory-occupancy point
│   └── parse_multicore.py    4-core equal-work heartbeat parser
└── plot/
    ├── plot_suite.py         bar chart from a parsed table
    ├── plot_utilsweep.py     line chart: speedup vs occupancy
    └── plot_table.py         render a parsed table as an image
```

Experiment definitions live outside this directory:
`experiments/clist_revelator.yaml` (the three single-core suites) and the
`ae_revelator_4core` suite in `experiments/clist_multicore.yaml`.

Component-level documentation for Revelator itself — engines, allocators, configs,
knobs — is in [`docs/revelator.md`](../../docs/revelator.md).
