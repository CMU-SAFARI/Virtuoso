# TRAIL — Artifact Evaluation

This guide reproduces the TRAIL TLB-prefetcher results inside a Docker container.
Everything runs from a few scripts; the traces are a public Hugging Face dataset.

---

## 0. Enter a clean-room container

The Docker image is a bare, pinned Ubuntu base — nothing is pre-installed, so you
run (and see) every setup step yourself.

```bash
docker build -t trail-ae \
  "https://github.com/CMU-SAFARI/Virtuoso.git#trail-artifact-release:experiments/ae"

# mount a host dir so the (~250 GB) trace download survives container restarts
mkdir -p ae_bundle
docker run -it -v "$PWD/ae_bundle":/work/ae_bundle trail-ae
```

Inside the container, clone the repo and install the dependencies:

```bash
apt-get update && apt-get install -y git
git clone --branch trail-artifact-release https://github.com/CMU-SAFARI/Virtuoso.git
cd Virtuoso
bash experiments/ae/lib/install_deps.sh      # apt toolchain + huggingface_hub + matplotlib
```

---

## 1. Setup + sanity check — one command

```bash
bash experiments/ae/reproduce.sh --skip-deps --bundle /work/ae_bundle
```

`--skip-deps` because you just ran `install_deps.sh`; `--bundle /work/ae_bundle`
puts the trace download on the mounted host dir. This runs, with progress, and
then **stops**:

```
[1/4] system build dependencies       (skipped)
[2/4] build the trace-replay simulator  (~2-3 min; no Pin/SDE/libtorch)
[3/4] download traces + trace-lists      (public HF dataset konkanello/trail_traces; resumable)
[4/4] validate the setup on 3 random traces  ->  [PASS] <trace> IPC=...
      "Setup is validated. You are ready to run the experiments."
```

Every phase is resumable — re-run the command after an interruption and it skips
whatever is already done. Flags: `--n N` (validation traces), `--skip-download`
(reuse a bundle), `--hf-repo`, `--bundle`.

The trace files are distributed flat as the public dataset
[`konkanello/trail_traces`](https://huggingface.co/datasets/konkanello/trail_traces)
(`traces/` + `vm_tlist/`); `reproduce.sh` downloads it and resolves the
trace-lists into `experiments/vm_tlist/` for you.

---

## 2–4. Reproduce a claim (launch → watch → results)

Each claim runs as three steps. Example for the headline **8 MB head-to-head**:

```bash
# 2. launch all jobs (non-blocking). On a SLURM cluster:
bash experiments/ae/ae_launch.sh  --claim head8mb --mode slurm --partitions <partition>
#    On a single machine (no SLURM):  --mode local --jobs $(nproc)

# 3. start the background watcher, then check progress whenever you like
bash experiments/ae/ae_watch.sh   --claim head8mb
cat  experiments/ae/ae_out/head8mb.status      # done / running / failed — any time

# 4. once the watcher writes ae_out/head8mb.DONE (green signal), parse + plot
bash experiments/ae/ae_results.sh --claim head8mb            # add --wait to block
#    -> experiments/ae/ae_out/head8mb.md   (table: ours vs paper)
#    -> experiments/ae/ae_out/head8mb.pdf  (paper-format figure)
```

The watcher writes a live `ae_out/<claim>.status` (done/running/failed) and, when
every job is accounted for, an `ae_out/<claim>.DONE` file with a **pass/fail
report** listing any failed run-dirs (and their `slurm.err`).

---

## Claims

| `--claim`   | reproduces                              | jobs  | figure |
|-------------|-----------------------------------------|------:|--------|
| `head8mb`   | Head-to-head @ 8 MB NUCA (main result)  | 2761  | family + GMEAN bars |
| `head2mb`   | Head-to-head @ 2 MB NUCA                | 2761  | family + GMEAN bars |
| `table5`    | Table 5 — in-PTE payload-budget sweep   | 5271  | bar of the table |
| `table6`    | Table 6 — side-car payload sweep        | 6275  | bar of the table |
| `pqsweep`   | L2-TLB PQ-size sensitivity              | 3012  | bar of the table |
| `multicore` | 4-core, 100-mix head-to-head            | 1000  | equal-work harmonic-mean bars |
| **all**     | everything above                        | **21080** | |

Each single-core job is a **300 M-instruction** sim (virtuoso-family traces run
100 M); multicore jobs run to the **equal-work 50 M-per-core** crossing. `all` is
cluster-scale — on a single machine, run one claim at a time (or start with
`head8mb`, the cheapest full claim and the headline result). If both `head8mb`
and `head2mb` have run, `ae_results.sh` emits the paper's 2×2 (2 MB over 8 MB)
figure.

Speedups are geomeans over the curated top-200 (non-`srv`) workloads; multicore
uses the equal-work heartbeat-crossing method (not `sim.stats` global cycles).

---

## Notes for reviewers

- **No Intel Pin / SDE / libtorch** are needed — this is the trace-replay build
  (`make SNIPER_TRACE_ONLY=1 replay`); the instruction decoder (xed) is fetched
  and built automatically.
- All experiments run on the **corrected cost model** (a prefetch that faults
  allocates the page *and* pays its page-table-walk latency).
- Without Docker: on any Debian/Ubuntu machine run
  `sudo experiments/ae/lib/install_deps.sh` then `bash experiments/ae/reproduce.sh`.
- `run_ae.sh --mode {slurm|local} --claim <c>` is the all-in-one alternative to
  the launch/watch/results trio (it launches, blocks until done, and parses).
