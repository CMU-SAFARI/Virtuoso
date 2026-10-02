# TRAIL — Artifact Evaluation

This artifact reproduces the main results of the TRAIL paper. TRAIL is a temporal
TLB prefetcher that makes each page-table
entry carry the translations that most often follow it, so one prefetch both
installs a translation and delivers the next prediction. The experiments compare
TRAIL against a no-prefetch baseline, prior TLB prefetchers (IP-Stride, Next-Page,
DP, Recency, ATP, Berti), and a Perfect-L2TLB upper bound on the Sniper
architectural simulator (with the MimicOS operating-system model), across
single-core (8 MB and 2 MB last-level-cache NUCA) and 4-core configurations.

Reproducing a result takes three steps: **(1) install dependencies → (2) build &
validate → (3) run the experiments.** The motivation figures (2, 3, 5, 6, 7) are
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
same `ae_run_all.sh` — all but `motivation` simulate, while `motivation` analyses
page-table-walk dumps instead. Outputs land in `experiments/ae/ae_out/` and, for
the motivation figures, `experiments/ae/motivation/motivation_out/`.

| suite / step | reproduces                                       |  jobs | output |
|--------------|--------------------------------------------------|------:|--------|
| `head2mb`    | Head-to-head @ 2 MB LLC per core                 |  3263 | **Figure 11** (top) + **Figure 13** |
| `head8mb`    | Head-to-head @ 8 MB LLC per core                 |  3263 | **Figure 11** (bottom) |
| `table5`     | In-PTE payload-budget sweep                      |  5271 | **Table 4** |
| `table6`     | TRAIL-External payload sweep                     |  6275 | **Table 5** |
| `multicore`  | 4-core, 100-mix head-to-head                     |  1200 | **Figure 16** |
| `head2mb_mtps400` … `head2mb_mtps4800` | DRAM bandwidth (400–4800 MT/s; 2400 is `head2mb`) | 3000 | **Figure 18** |
| `abl2mb`     | TRAIL component ablation @ 2 MB LLC per core     |   600 | **Figure 19** |
| `motivation` | Temporal-locality characterization (no simulation) | 251 | **Figures 2, 3, 5, 6, 7** |
| **all**      | every suite above, the default                   | **23123** | |

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

**A suite is finished when its results are on disk, not when the queue is empty.**
`done` counts jobs that produced a valid result — a `sim.stats` containing a
cycle count, or a complete per-workload JSON — and once that reaches the expected
number the watcher writes `DONE` immediately. This matters on a busy cluster,
where SLURM can hold finished jobs in `CG` (completing) for many minutes after
they have written their output; you will see `FINISHING (all N results written;
K job(s) still clearing the queue)` and can move straight to 3.3. If instead the
queue drains while results are missing, those jobs failed and the `DONE` report
lists them.

### 3.3 — Produce the figures and tables

Once 3.2 shows every suite at `DONE`:

```bash
bash experiments/ae/ae_run_all.sh --results
```

This parses each finished suite and renders its figure or table, including
Figures 2, 3, 5, 6, 7 from the motivation suite. It skips anything not finished
yet and says so, so it is safe to re-run as the remaining suites land.

### What you get (SLURM or single machine)

`ae_run_all.sh --results` writes each figure or table to `ae_out/` as a PDF (and
PNG) with its numbers next to it in a Markdown file: `figure11` (one row per LLC
size; both rows once `head2mb` and `head8mb` have run), `figure13`, `figure16`,
`figure18` (every DRAM speed that has run), `figure19`, `table4` and `table5`. Progress lives in `ae_out/<suite>.status`, and each
finished suite gets an `ae_out/<suite>.DONE` **pass/fail report** (listing any
failed jobs and where to find their logs).

On the motivation side, the 3.1 analysis writes one JSON per workload to
`motivation_out/json/` and one TLB-simulation result to `motivation_out/tlbsim/`
(Figure 6), and 3.3's `--plot` renders Figures 2, 3, 5, 6, 7 into
`experiments/ae/motivation/motivation_out/` (see
[`motivation/README.md`](motivation/)).

**Scale & runtime:** a single-core job simulates 300 M instructions (a few
minutes to under an hour each); `all` is 22,872 simulations. On a **~1300-core cluster
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

The same three scripts drive `--suite motivation`; they hand off to
`motivation/run_motivation.sh` underneath, so recovery works identically. A
workload is redone unless its JSON is complete, which also repairs one truncated
by a job that was killed mid-write.

---

## Expected results

Exact numbers vary slightly with the machine, but each simulation suite should
closely match the paper. Speedups are geometric-mean over the workload suite
(multicore is equal-work harmonic-mean across the 4 cores):

| suite | expected (approx.) |
|-------|--------------------|
| `head2mb` (Figure 11 top, Figure 13) | **TRAIL ≈ +5.7%** over No-TLB-Prefetcher, **+1.6%** over the best prior prefetcher (Recency); Perfect-L2TLB ≈ +16.7%. On the 50 most translation-intensive workloads TRAIL cuts total demand page-table-walk latency by ≈ 60% (Recency ≈ 42%) |
| `head8mb` (Figure 11 bottom) | **TRAIL ≈ +5.2%** over No-TLB-Prefetcher, **+1.8%** over Recency; Perfect-L2TLB ≈ +12.0% |
| `table5`  (Table 4)  | in-PTE TRAIL grows with the number of deltas, up to **≈ +5.15%** |
| `table6`  (Table 5)  | TRAIL-External grows with the payload, up to **≈ +5.26%** |
| `multicore` (Figure 16) | **TRAIL ≈ +11.5%** harmonic-mean, Recency ≈ +8.7%; with Next-Page ≈ +14.1%; Perfect-L2TLB ≈ +23.1% |
| `head2mb_mtps*` (Figure 18) | TRAIL above Recency at every DRAM speed: ≈ +1.7% vs +0.7% at 400 MT/s, ≈ +5.2% vs +3.5% from 1600 MT/s up |
| `abl2mb` (Figure 19) | top-50 workloads: global deltas ≈ +9.1%, + PC table ≈ +10.6%, + virtualized PC table ≈ +10.7%, full TRAIL ≈ +11.1% |

In every suite **TRAIL should beat all prior prefetchers** (IP-Stride, Next-Page,
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
