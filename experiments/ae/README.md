# TRAIL — Artifact Evaluation

This artifact reproduces the main results of the TRAIL paper. TRAIL is a temporal
TLB prefetcher that makes each page-table
entry carry the translations that most often follow it, so one prefetch both
installs a translation and delivers the next prediction. The experiments compare
TRAIL against a no-prefetch baseline, prior TLB prefetchers (ASP, Stride/NextPage,
DP, Recency, ATP, Berti), and a Perfect-L2TLB upper bound on the Sniper
architectural simulator (with the MimicOS operating-system model), across
single-core (8 MB and 2 MB last-level-cache NUCA) and 4-core configurations.

Reproducing a result takes three steps: **(1) install dependencies → (2) build &
validate → (3) run the experiments.** The motivation figures (4, 5, 6, 8, 9) are
a separate, trace-free step — two commands (analyse, then plot) over a set of
page-table-walk dumps, with no simulation involved.

---

## Requirements

**Software**
- Linux (Debian/Ubuntu recommended; tested on Ubuntu 20.04, 22.04 and 24.04).
  Other distributions work if you install the equivalent packages. On 24.04 all
  harness Python runs from a self-contained virtualenv, so PEP 668
  ("externally-managed-environment") never gets in the way.
- A C++17 toolchain and Python 3.8+. All packages — plus the Hugging Face CLI
  (trace download) and matplotlib (figures) — are installed by
  `experiments/ae/lib/install_deps.sh`. No license-gated or proprietary tools are
  required; the instruction decoder is fetched and built automatically.
- Network access (to download the traces and fetch the decoder during the build).
- *Optional:* a SLURM cluster to run the full experiments in parallel. Without
  SLURM everything still runs locally.

**Hardware**
- **Disk:** ~300 GB free (≈250 GB of traces, plus the build and results). If the
  traces are already staged for you at `<parent-of-clone>/ae_bundle`, only a few
  GB are needed — see Step 2. The motivation figures need a further ~3 GB of
  page-table-walk dumps, likewise skipped if a bundle is already staged (see
  [`motivation/README.md`](motivation/)).
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

## Step 2 — Build & validate (one-command setup + sanity check)

```bash
bash experiments/ae/build_and_validate.sh --skip-deps
```

This builds the simulator, downloads the datasets, and runs a few short
simulations on randomly chosen traces to confirm the whole pipeline works. It
shows progress and then **stops**:

```
[1/4] system build dependencies       (skipped — done in Step 1)
[2/4] build the simulator             (~2-3 min)
[3/4] traces + trace-lists             (skipped if ../ae_bundle is already there;
                                        else public dataset; ~250 GB; resumable)
      + PTW dumps for the motivation   (skipped if a ptw_bundle is already there;
        figures                         else ~2.9 GB; --skip-ptw-dumps opts out)
[4/4] validate on 3 random traces      ->  [PASS] <trace> IPC=...
      "Setup is validated. You are ready to run the experiments."
```

**All downloading happens here.** The run scripts (`ae_*`, `run_motivation.sh`)
never fetch anything, so once this step succeeds every later command works
offline against what is on disk.

Every phase is resumable — re-run the command after an interruption and it skips
whatever is already done. **This step alone is enough to confirm the artifact
builds and runs;** the full paper numbers come from Step 3.

**If the traces are already on the machine, there is no download.** When a valid
bundle sits one level *above* the clone — `<parent-of-Virtuoso>/ae_bundle`, with
`traces/` and `vm_tlist/` inside — Step 2 detects it, prints

```
found a pre-staged trace bundle: /path/to/ae_bundle
(251 traces, 10 trace-lists) — the download is not needed.
```

and uses it **in place**, writing nothing. This is the normal case when your host
has already fetched the dataset for you: Step 2 then costs only the build plus the
sanity check. Nothing else changes — the trace-lists are resolved to absolute paths,
so the bundle does not have to live inside the clone. Use `--bundle-mode link` to
drop a symlink at `Virtuoso/ae_bundle` instead, or `--bundle-mode copy` to take a
full private copy (this duplicates the whole ~250 GB trace set — only do it if you
need the bundle on faster local storage).

Useful flags: `--n N` (number of validation traces), `--skip-download` (reuse an
existing download), `--bundle DIR` (where to download), `--bundle-mode
reuse|link|copy` (how to consume a pre-staged bundle; default `reuse`). Traces are
the public dataset
[`konkanello/trail_traces`](https://huggingface.co/datasets/konkanello/trail_traces)
(`traces/` + `vm_tlist/`); `build_and_validate.sh` downloads it and wires the trace-lists
into `experiments/vm_tlist/` for you.

---

## Step 3 — Reproduce the paper results

Every row below is one paper artifact, and every row is a **suite** driven by the
same `ae_run_all.sh` — six of them simulate, while `motivation` analyses
page-table-walk dumps instead. Outputs land in `experiments/ae/ae_out/` and, for
the motivation figures, `experiments/ae/motivation/motivation_out/`.

| suite / step | reproduces                                       |  jobs | output |
|--------------|--------------------------------------------------|------:|--------|
| `head8mb`    | Head-to-head @ 8 MB NUCA (headline)              |  2761 | **Figure 12** (bottom) + **Figure 13** (mechanism) |
| `head2mb`    | Head-to-head @ 2 MB NUCA                         |  2761 | **Figure 12** (top) |
| `table5`     | In-PTE payload-budget sweep                      |  5271 | **Table 5** |
| `table6`     | Side-car payload sweep                           |  6275 | **Table 6** |
| `pqsweep`    | L2-TLB prefetch-queue sensitivity                |  3012 | **Figure 20** |
| `multicore`  | 4-core, 100-mix head-to-head                     |  1000 | **Figure 22** |
| `motivation` | Temporal-locality characterization (no simulation) | 251 | **Figures 4, 5, 6, 8, 9** |
| **all**      | every suite above, the default                   | **21331** | |

The order is the same everywhere: **launch everything, check progress, then
produce the figures.** Nothing plots by itself, and the plotting passes only work
once the jobs have finished — so run 3.1, come back later for 3.2, and finish with
3.3. In normal operation you never touch the individual
`ae_launch`/`ae_watch`/`ae_results` scripts; those are only for recovery if a suite
fails (see
[**If something goes wrong**](#if-something-goes-wrong--manual-per-suite-control),
below).

### 3.1 — Launch everything

One command. `motivation` is a suite like any other, so this covers every row of
the table above — the six simulation suites *and* the temporal-locality analysis.

**A) On a SLURM cluster (recommended):**

```bash
bash experiments/ae/ae_run_all.sh --mode slurm --partitions <partition>
```

Suites are independent and proceed in parallel; `motivation` submits one job per
workload alongside them. `--partitions` is optional (omit it to use your cluster's
default; no `--partition` is passed to `sbatch`). Restrict the set with
`--suites "head8mb motivation"`.

**B) On a single machine (no SLURM):**

```bash
bash experiments/ae/ae_run_all.sh --mode local --jobs $(nproc)
```

One shared scheduler runs everything through this machine's cores — simulations
and motivation analyses in the same pool — keeping at most `--jobs` (default:
cores − 2) running at a time, so the machine is never oversubscribed no matter how
many suites you launch. A full 300 M-instruction suite is large for a single host:
add `--icount 2000000` to shrink every simulation to a minutes-long end-to-end
smoke test (the numbers won't match the paper at 2 M instructions; it does not
affect `motivation`). `--icount` works in SLURM mode too.

Everything reads input that is already on disk — the traces and the PTW dumps
staged by Step 2. The motivation suite prints which dump bundle it picked
(`$HOME/ptw_bundle`, `../ptw_bundle` or `./ptw_bundle`); if it reports none, re-run
Step 2 without `--skip-ptw-dumps`. Nothing in Step 3 ever downloads.

### 3.2 — Check progress

Any time, as often as you like:

```bash
bash experiments/ae/ae_run_all.sh --status
```

Every suite reports `done: N / M` and a `status:` line; wait until all of them
read `DONE`. For `motivation` the count is analysed workloads (251) rather than
simulation jobs.

### 3.3 — Produce the figures and tables

Once 3.2 shows every suite at `DONE`:

```bash
bash experiments/ae/ae_run_all.sh --results
```

This parses each finished suite and renders its figure or table, including
Figures 4, 5, 6, 8, 9 from the motivation suite. It skips anything not finished
yet and says so, so it is safe to re-run as the remaining suites land.

### What you get (SLURM or single machine)

`ae_run_all.sh --results` writes, for each suite, a table (`ae_out/<suite>.md`)
and a rendered image in `ae_out/`: `figure12.pdf` (the 2×2 plot, once **both**
`head8mb` and `head2mb` have run), `figure13.pdf`, `figure20.pdf`, `figure22.pdf`,
and `table5.pdf`/`table6.pdf`. Progress lives in `ae_out/<suite>.status`, and each
finished suite gets an `ae_out/<suite>.DONE` **pass/fail report** (listing any
failed jobs and where to find their logs).

On the motivation side, the 3.1 analysis writes one JSON per workload to
`motivation_out/json/`, and 3.3's `--plot` renders Figures 4, 5, 6, 8, 9 into
`experiments/ae/motivation/motivation_out/` (see
[`motivation/README.md`](motivation/)).

**Scale & runtime:** a single-core job simulates 300 M instructions (a few
minutes to under an hour each); `all` is 21,080 jobs. On a **~1300-core cluster
the entire set finishes within ~1 day**, and the longest single suite (`table6`)
takes **~10 hours**. On a single machine the shared scheduler spreads whatever you
launch across your cores; a full local run is still large, so use `--icount` for a
quick end-to-end pass.

### If something goes wrong — manual per-suite control

**You normally never run these.** `ae_run_all.sh` already launches, watches, and
parses every suite. Reach for the per-suite scripts **only to recover a suite that
reported failures** — to re-launch just that suite, re-attach a watcher, or
re-parse its existing results:

```bash
bash experiments/ae/ae_launch.sh  --suite head8mb --mode slurm --partitions <partition>
#                                  single machine instead:  --mode local --jobs $(nproc)
bash experiments/ae/ae_watch.sh   --suite head8mb
bash experiments/ae/ae_results.sh --suite head8mb --wait
```

Re-running `ae_launch.sh` only resubmits jobs that do not yet have a valid result,
so it cleanly heals a partially-failed suite without repeating finished work.

---

## Expected results

Exact numbers vary slightly with the machine, but each simulation suite should
closely match the paper. Speedups are geometric-mean over the workload suite
(multicore is equal-work harmonic-mean across the 4 cores):

| suite | expected (approx.) |
|-------|--------------------|
| `head8mb` (Figure 12,13, 8 MB) | **TRAIL ≈ +4.7%** over no-prefetch (**+2.4%** over ASP); Perfect-L2TLB ≈ +11% (upper bound) |
| `head2mb` (Figure 12, 2 MB) | **TRAIL ≈ +5.1%** over no-prefetch (**+2.8%** over ASP); Perfect-L2TLB ≈ +16.5% |
| `table5`  (Table 5)         | TRAIL (in-PTE) grows with the payload budget, up to **≈ +2.4%** over ASP |
| `table6`  (Table 6)         | TRAIL (side-car) grows with the payload budget, up to **≈ +2.5%** over ASP |
| `pqsweep` (Figure 20)       | **TRAIL ≈ +2.2–2.4%** over the same-size ASP across every PQ size (64→1024) |
| `multicore` (Figure 22)     | **TRAIL ≈ +11%** harmonic-mean (best prior prefetcher ≈ +5%); Perfect-L2TLB ≈ +23% |

In every suite **TRAIL should beat all prior prefetchers** (ASP, Stride/NextPage,
DP, Recency, ATP, Berti) and move toward the Perfect-L2TLB upper bound. The
`motivation` figures are characterization (temporal locality of TLB-miss
successors), so they carry no speedup number.

---

## Notes

- Re-running a suite only re-submits jobs that do not yet have a valid result, so
  an interrupted run resumes cleanly.
- `build_and_validate.sh` downloads the public dataset by default (no token, no
  `--hf-repo` needed). Override with `--hf-repo <owner/name>` only if you host a
  mirror.
- A self-contained HTML **code walkthrough** of how TRAIL works (the
  `TemporalPTEPrefetcher`) is in
  [`docs/trail_walkthrough.html`](../../docs/trail_walkthrough.html) — open it in a
  browser.
- **Archived snapshot:** Zenodo DOI
  [`10.5281/zenodo.21541804`](https://doi.org/10.5281/zenodo.21541804).
