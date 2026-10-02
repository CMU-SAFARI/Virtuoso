# Motivation analysis (Figures 2, 3, 5, 6, 7)

These figures characterise the temporal-locality of TLB-miss successors that
motivates TRAIL. They are produced from **page-table-walk (PTW) dumps** — one
CSV per workload of `(PTW_Address, EIP, …)` — not from full simulations.

| figure | what it shows |
|--------|---------------|
| **Figure 2** | Transitions captured by the top-k most frequent deltas per region (global vs. per-PC) |
| **Figure 3** | Per-PC delta distributions of selected 32 KB source regions, 4 example workloads |
| **Figure 5** | Captured transitions vs. destination-region size (top-4 / top-8) |
| **Figure 6** | Used vs. unused prefetched PTEs in a 1536-entry, 4-way TLB, vs. region size |
| **Figure 7** | % of representable delta occurrences vs. delta bit-width |

Figures 2, 5, 6 and 7 average over the 200 workloads with the highest per-PC
coverage at k=4. Figure 3 plots the source regions listed in
`data/figure3_selected_regions.csv` (selected from the same dumps).

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
bash experiments/ae/ae_run_all.sh --results                       # renders the figures
```

Restrict it to this suite alone with `--suites motivation`. Progress, the
`ae_out/motivation.DONE` pass/fail report and the results gate work exactly as
they do for a simulation suite; the count is analysed workloads rather than jobs.

## 3. The direct way — analyse, then plot

`--dumps` is optional; omit it to use the pre-staged bundle found above.

**Step 1 — analyse** (one job per workload; never plots). Each job runs
`analyze_dump.py` and then `tlb_prefetch_sim` (Figure 6), which the script compiles
on first use (`g++ -std=c++17`):

```bash
# local: analyse on this machine with up to N cores
bash experiments/ae/motivation/run_motivation.sh --mode local --jobs $(nproc)

# or on a SLURM cluster: submits one job per workload and returns immediately
bash experiments/ae/motivation/run_motivation.sh --mode slurm [--partitions <p>]

# explicit path still works and overrides the search
bash experiments/ae/motivation/run_motivation.sh --dumps ./ptw_bundle --mode local
```

**Step 2 — plot**, once step 1 has finished (in SLURM mode, once the jobs are done —
check with `ls motivation_out/tlbsim/*.json | wc -l`, which should reach 251):

```bash
bash experiments/ae/motivation/run_motivation.sh --plot
```

Step 2 is separate on purpose: in SLURM mode the analysis only *submits* the jobs,
so the figures cannot exist yet. It refuses to draw incomplete figures and reports
how many workloads are ready; pass `--allow-partial` to plot anyway. Both steps are
resumable — re-running step 1 only analyses what is still missing.

Output goes to `experiments/ae/motivation/motivation_out/`:
`figure2_topk_coverage`, `figure3_delta_distributions`, `figure5_granularity`,
`figure6_pte_fate`, `figure7_delta_bits` (each `.pdf` + `.png`).

The analysis is resumable — each workload writes one JSON under
`motivation_out/json/` and one under `motivation_out/tlbsim/`; a re-run skips
workloads that have both. In
SLURM mode, submit the jobs, wait for them to finish, then run the same command
again to produce the figures.

## Pieces
- `analyze_dump.py` — reads one dump, computes all per-workload metrics (one pass).
- `run_motivation.sh` — SLURM/local driver: analysis (default) and `--plot`, as two steps.
- `tlb_prefetch_sim.cc` — replays one dump through a TLB with a region prefetcher (Figure 6).
- `plot_motivation.py` — renders the five figures.
