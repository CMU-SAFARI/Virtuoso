# TRAIL — Artifact Evaluation

This artifact reproduces the main results of the TRAIL paper. TRAIL is a temporal
TLB prefetcher; the experiments compare it against prior TLB prefetchers on the
Sniper architectural simulator (with the MimicOS operating-system model), across
single-core (8 MB and 2 MB last-level-cache NUCA) and 4-core configurations.

Reproducing a result takes three steps: **install dependencies → run a
one-command setup + sanity check → launch the experiments.**

---

## Requirements

**Software**
- Linux (Debian/Ubuntu recommended; tested on Ubuntu 20.04 and 22.04). Other
  distributions work if you install the equivalent packages.
- A C++17 toolchain and Python 3.8+. All packages — plus the Hugging Face CLI
  (trace download) and matplotlib (figures) — are installed by
  `experiments/ae/lib/install_deps.sh`. No license-gated or proprietary tools are
  required; the instruction decoder is fetched and built automatically.
- Network access (to download the traces and fetch the decoder during the build).
- *Optional:* a SLURM cluster to run the full experiments in parallel. Without
  SLURM everything still runs locally.

**Hardware**
- **Disk:** ~300 GB free (≈250 GB of traces, plus the build and results).
- **RAM:** 8 GB is enough for the build and the sanity check. Each simulation
  uses a few GB, so budget more if you run many in parallel.
- **CPU:** any x86-64 machine. More cores (or a cluster) only reduce wall time.

---

## Step 1 — Install dependencies

```bash
git clone --branch trail-artifact-release https://github.com/CMU-SAFARI/Virtuoso.git
cd Virtuoso
bash experiments/ae/lib/install_deps.sh        # uses sudo if you are not root
```

---

## Step 2 — Setup + sanity check (minimal check that the artifact works)

```bash
bash experiments/ae/reproduce.sh --skip-deps
```

This builds the simulator, downloads the trace dataset, and runs a few short
simulations on randomly chosen traces to confirm the whole pipeline works. It
shows progress and then **stops**:

```
[1/4] system build dependencies       (skipped — done in Step 1)
[2/4] build the simulator             (~2-3 min)
[3/4] download traces + trace-lists    (public dataset; ~250 GB; resumable)
[4/4] validate on 3 random traces      ->  [PASS] <trace> IPC=...
      "Setup is validated. You are ready to run the experiments."
```

Every phase is resumable — re-run the command after an interruption and it skips
whatever is already done. **This step alone is enough to confirm the artifact
builds and runs;** the full paper numbers come from Step 3.

Useful flags: `--n N` (number of validation traces), `--skip-download` (reuse an
existing download), `--bundle DIR` (where to download). Traces are the public
dataset
[`konkanello/trail_traces`](https://huggingface.co/datasets/konkanello/trail_traces)
(`traces/` + `vm_tlist/`); `reproduce.sh` downloads it and wires the trace-lists
into `experiments/vm_tlist/` for you.

---

## Step 3 — Reproduce the paper results

Every row below is one paper artifact. The six **simulation claims** are driven by
the `ae_*` harness; the **motivation** figures are produced from page-table-walk
dumps (no simulation). Outputs land in `experiments/ae/ae_out/` (simulation) and
`experiments/ae/motivation/motivation_out/` (motivation).

| claim / step | reproduces                                       |  jobs | output |
|--------------|--------------------------------------------------|------:|--------|
| `head8mb`    | Head-to-head @ 8 MB NUCA (headline)              |  2761 | **Figure 12** (bottom) + **Figure 13** (mechanism) |
| `head2mb`    | Head-to-head @ 2 MB NUCA                         |  2761 | **Figure 12** (top) |
| `table5`     | In-PTE payload-budget sweep                      |  5271 | **Table 5** |
| `table6`     | Side-car payload sweep                           |  6275 | **Table 6** |
| `pqsweep`    | L2-TLB prefetch-queue sensitivity                |  3012 | **Figure 20** |
| `multicore`  | 4-core, 100-mix head-to-head                     |  1000 | **Figure 22** |
| `motivation` | Temporal-locality characterization (no simulation) |   — | **Figures 4, 5, 6, 8, 9** |
| **all sim.** | every simulation claim above                     | **21080** | |

### Simulation claims

All claims in parallel (recommended on a cluster):

```bash
bash experiments/ae/ae_run_all.sh --mode slurm --partitions <partition>  # launch every claim
bash experiments/ae/ae_run_all.sh --status                                # progress, any time
bash experiments/ae/ae_run_all.sh --results                               # tables + figures
```

Claims are independent and proceed in parallel. Restrict the set with
`--claims "head8mb multicore"`. Use `--mode local` (single machine, no SLURM)
**one claim at a time** — local simulations all share this machine's CPUs.

One claim at a time:

```bash
bash experiments/ae/ae_launch.sh  --claim head8mb --mode slurm --partitions <partition>
bash experiments/ae/ae_watch.sh   --claim head8mb
cat  experiments/ae/ae_out/head8mb.status         # done / running / failed — any time
bash experiments/ae/ae_results.sh --claim head8mb # after ae_out/head8mb.DONE (add --wait to block)
```

The watcher writes a live `ae_out/<claim>.status` and, when every job is
accounted for, an `ae_out/<claim>.DONE` file with a **pass/fail report** (it lists
any failed jobs and where to find their logs). `ae_results.sh` then writes the
table and the rendered image: `figure12.pdf` (the 2×2 plot, once **both**
`head8mb` and `head2mb` have run), `figure13.pdf`, `figure20.pdf`, `figure22.pdf`,
and `table5.pdf`/`table6.pdf`.

Options for `ae_launch.sh` / `ae_run_all.sh`:
- `--partitions p1,p2` is optional — omit to use your cluster's default partition
  (no `--partition` is passed to `sbatch`).
- `--icount N` shrinks every job's instruction budget (default 300 M) for a fast
  end-to-end smoke test, e.g. `--icount 2000000` — confirms the whole
  launch → watch → results pipeline in minutes (numbers won't match the paper).

**Scale & runtime:** a single-core job simulates 300 M instructions (a few
minutes to under an hour each); `all` is 21,080 jobs. On a **~1300-core cluster
the entire set finishes within ~1 day**, and the longest single claim (`table6`)
takes **~10 hours**. On a single machine, run one claim at a time — start with
**`head8mb`**, the headline result and the cheapest full claim.

### Motivation figures

Produced from a separate ~3 GB public dataset of page-table-walk dumps, without
running any simulation:

```bash
hf download konkanello/trail_ptw_dumps --repo-type dataset --local-dir ./ptw_bundle
bash experiments/ae/motivation/run_motivation.sh --dumps ./ptw_bundle --mode local --jobs $(nproc)
```

This writes Figures 4, 5, 6, 8, 9 to `experiments/ae/motivation/motivation_out/`.
Use `--mode slurm [--partitions <p>]` to run one job per workload instead. See
[`motivation/README.md`](motivation/) for details.

---

## Expected results

Exact numbers vary slightly with the machine, but each simulation claim should
closely match the paper. Speedups are geometric-mean over the workload suite
(multicore is equal-work harmonic-mean across the 4 cores):

| claim | expected (approx.) |
|-------|--------------------|
| `head8mb` (Figure 12, 8 MB) | **TRAIL ≈ +4.7%** over no-prefetch (**+2.4%** over ASP); Perfect-L2TLB ≈ +11% (upper bound) |
| `head2mb` (Figure 12, 2 MB) | **TRAIL ≈ +5.1%** over no-prefetch (**+2.8%** over ASP); Perfect-L2TLB ≈ +16.5% |
| `table5`  (Table 5)         | TRAIL (in-PTE) grows with the payload budget, up to **≈ +2.4%** over ASP |
| `table6`  (Table 6)         | TRAIL (side-car) grows with the payload budget, up to **≈ +2.5%** over ASP |
| `pqsweep` (Figure 20)       | **TRAIL ≈ +2.2–2.4%** over the same-size ASP across every PQ size (64→1024) |
| `multicore` (Figure 22)     | **TRAIL ≈ +11%** harmonic-mean (best prior prefetcher ≈ +5%); Perfect-L2TLB ≈ +23% |

In every claim **TRAIL should beat all prior prefetchers** (ASP, Stride/NextPage,
DP, Recency, ATP, Berti) and move toward the Perfect-L2TLB upper bound. The
`motivation` figures are characterization (temporal locality of TLB-miss
successors), so they carry no speedup number.

---

## Notes

- Re-running a claim only re-submits jobs that do not yet have a valid result, so
  an interrupted run resumes cleanly.
- `reproduce.sh` downloads the public dataset by default (no token, no
  `--hf-repo` needed). Override with `--hf-repo <owner/name>` only if you host a
  mirror.
- A ready-to-include **artifact appendix** for the paper is in
  [`artifact_appendix.tex`](artifact_appendix.tex).
