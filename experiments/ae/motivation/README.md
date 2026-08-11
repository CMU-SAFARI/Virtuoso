# Motivation analysis (Figures 4, 5, 6, 8, 9)

These figures characterise the temporal-locality of TLB-miss successors that
motivates TRAIL. They are produced from **page-table-walk (PTW) dumps** — one
CSV per workload of `(PTW_Address, EIP, …)` — not from full simulations.

| figure | what it shows |
|--------|---------------|
| **Figure 4** | CDF of unique `(PC, Δ)` pairs per workload |
| **Figure 5** | Top-k successor coverage (global vs. PC-conditioned) |
| **Figure 6** | Fraction of transitions per source region, for 4 example workloads |
| **Figure 8** | Successor-region granularity sweep (top-4 / top-8) |
| **Figure 9** | % of representable delta occurrences vs. delta bit-width |

## 1. Get the dumps

**First check whether they are already on the machine.** `run_motivation.sh` looks
for a pre-staged bundle before anything else, in this order:

```
$HOME/ptw_bundle          <- usual place on a shared machine (often a symlink)
<artifact>/../ptw_bundle
<artifact>/ptw_bundle
```

If one is there the script says so and the download is skipped. To share a single
copy between accounts, symlink it:

```bash
ln -s /path/to/shared/ptw_bundle ~/ptw_bundle
```

Otherwise **setup fetches them for you** — they are a public Hugging Face dataset
(~3 GB, 251 workloads), downloaded in phase [3/4] of the build:

```bash
bash experiments/ae/build_and_validate.sh --skip-deps
#   -> ./ptw_bundle/ptw_dumps/<workload>.csv.gz
```

That is the same command that stages the traces, and it is resumable, so re-run it
if the transfer is interrupted. The download is skipped when a bundle is already
present, and `--skip-ptw-dumps` opts out entirely (the six simulation suites do not
use these dumps — only the motivation figures do).

`run_motivation.sh` itself never downloads: the run scripts only run. Do not call
`hf` by hand either — it lives in the AE virtualenv, which the harness puts on its
own `PATH` but your interactive shell does not, so a bare `hf download` fails with
*command not found*.

## 2. The easy way — through the AE harness

`motivation` is a suite like `head8mb` or `multicore`, so the normal three passes
cover it and you do not need the commands in this file at all:

```bash
bash experiments/ae/ae_run_all.sh --mode slurm --partitions <p>   # or --mode local --jobs $(nproc)
bash experiments/ae/ae_run_all.sh --status                        # wait for DONE
bash experiments/ae/ae_run_all.sh --results                       # renders the five figures
```

Restrict it to this suite alone with `--suites motivation`. Progress, the
`ae_out/motivation.DONE` pass/fail report and the results gate work exactly as
they do for a simulation suite; the count is analysed workloads rather than jobs.

## 3. The direct way — analyse, then plot

`--dumps` is optional; omit it to use the pre-staged bundle found above.

**Step 1 — analyse** (one job per workload; never plots):

```bash
# local: analyse on this machine with up to N cores
bash experiments/ae/motivation/run_motivation.sh --mode local --jobs $(nproc)

# or on a SLURM cluster: submits one job per workload and returns immediately
bash experiments/ae/motivation/run_motivation.sh --mode slurm [--partitions <p>]

# explicit path still works and overrides the search
bash experiments/ae/motivation/run_motivation.sh --dumps ./ptw_bundle --mode local
```

**Step 2 — plot**, once step 1 has finished (in SLURM mode, once the jobs are done —
check with `ls motivation_out/json/*.json | wc -l`, which should reach 251):

```bash
bash experiments/ae/motivation/run_motivation.sh --plot
```

Step 2 is separate on purpose: in SLURM mode the analysis only *submits* the jobs,
so the figures cannot exist yet. It refuses to draw incomplete figures and reports
how many workloads are ready; pass `--allow-partial` to plot anyway. Both steps are
resumable — re-running step 1 only analyses what is still missing.

Output goes to `experiments/ae/motivation/motivation_out/`:
`figure4_pc_delta_pairs`, `figure5_topk_coverage`, `figure6_transitions_4wl`,
`figure8_granularity`, `figure9_delta_representable` (each `.pdf` + `.png`).

The analysis is resumable — each workload writes one JSON under
`motivation_out/json/`; a re-run skips completed workloads and (re)plots. In
SLURM mode, submit the jobs, wait for them to finish, then run the same command
again to produce the figures.

## Pieces
- `analyze_dump.py` — reads one dump, computes all per-workload metrics (one pass).
- `run_motivation.sh` — SLURM/local driver: analysis (default) and `--plot`, as two steps.
- `plot_motivation.py` — renders the five figures (default matplotlib font).
