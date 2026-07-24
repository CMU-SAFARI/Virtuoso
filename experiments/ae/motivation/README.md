# Motivation analysis (Figures 4, 5, 6, 8, 9)

These figures characterise the temporal-locality of TLB-miss successors that
motivates TRAIL. They are produced from **page-table-walk (PTW) dumps** — one
CSV per workload of `(PTW_Address, EIP, …)` — not from full simulations.

| figure | what it shows |
|--------|---------------|
| **Figure 4** | Unique `(PC, Δ)` pairs per workload vs. resident footprint |
| **Figure 5** | Top-k successor coverage (global vs. PC-conditioned) |
| **Figure 6** | Fraction of transitions per source region, for 4 example workloads |
| **Figure 8** | Successor-region granularity sweep (top-4 / top-8) |
| **Figure 9** | % of representable delta occurrences vs. delta bit-width |

## 1. Download the dumps

The dumps are a public Hugging Face dataset (~3 GB, 251 workloads):

```bash
hf download konkanello/trail_ptw_dumps --repo-type dataset --local-dir ./ptw_bundle
#   -> ./ptw_bundle/ptw_dumps/<workload>.csv.gz
```

## 2. Analyse + plot — one command

```bash
# local: analyse on this machine with up to N cores, then plot
bash experiments/ae/motivation/run_motivation.sh --dumps ./ptw_bundle --mode local --jobs $(nproc)

# or on a SLURM cluster: one job per workload, then re-run to plot once done
bash experiments/ae/motivation/run_motivation.sh --dumps ./ptw_bundle --mode slurm [--partitions <p>]
```

Output goes to `experiments/ae/motivation/motivation_out/`:
`figure4_pc_delta_pairs`, `figure5_topk_coverage`, `figure6_transitions_4wl`,
`figure8_granularity`, `figure9_delta_representable` (each `.pdf` + `.png`).

The analysis is resumable — each workload writes one JSON under
`motivation_out/json/`; a re-run skips completed workloads and (re)plots. In
SLURM mode, submit the jobs, wait for them to finish, then run the same command
again to produce the figures.

## Pieces
- `analyze_dump.py` — reads one dump, computes all per-workload metrics (one pass).
- `run_motivation.sh` — SLURM/local driver + plotting.
- `plot_motivation.py` — renders the five figures (default matplotlib font).
