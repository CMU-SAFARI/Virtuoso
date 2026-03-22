# Experiments Workflow 🚀

This README documents the end-to-end flow for configuring, launching, monitoring, and relaunching experiment suites in this artifact. It focuses on the scripts in the `experiments/` and `experiment_scripts/` folders.

## Overview 🗺️

- Central config: `clist.yaml` defines trace lists, configs, and experiment suites.
- Create jobfiles: `create_experiments.py` generates SBATCH jobfiles for suites.
- Safe submit: `safe_submit.py` validates and submits jobfiles with SLURM throttling.
- Monitor: `get_experiments_status.py` reports progress and errors.
- Rerun: `create_rerun_experiments.py` produces jobfiles to relaunch missing/failed runs.

## Prerequisites 📦

- Sniper simulator built and accessible.
- SLURM environment available for submission.
- Trace files reachable via paths contained in `.tlist` files.
- Python 3.8+.

## 0) Define `clist.yaml` 🧾

Put `clist.yaml` in `experiments/` (or provide a path to it) describing:

- `tracelists`: Named collections of `.tlist` files.
- `configs`: Configurations to be simulated.
- `suites`: Named combinations of traces, configs, and number of instructions.

### Example: Structure

```yaml
# experiments/clist.yaml
trace_suite:
  all_vm:
    tracelist_base_path: "vm_tlist/"
    tracelists:
        - dpc3.tlist
        - victima.tlist

  victima:
    tracelist_base_path: "vm_tlist/"
    tracelists:
      - victima.tlist

configs:
  configs_base_path: "../simulator/sniper/config/address_translation_schemes/"
  no_translation:
    name: "no-translation"
    file: "no_translation.cfg"
    description: "Translation disabled - VA used directly as PA"

  reservethp-full-fragmentation:
    name: "reservethp-frag0.0"
    file: "reservethp.cfg"
    extends:
      - "--perf_model/reserve_thp/target_fragmentation=0.0"
    description: "Reservation-based THP allocator with full fragmentation"

  reservethp_sweep1:
    name: "reservethp"
    file: "reservethp.cfg"
    sweeps:
      - identifier: ["frag", "promotionthresh"]
        option: ["--perf_model/reserve_thp/target_fragmentation=", "--perf_model/reserve_thp/threshold_for_promotion="]
        values: [[0.0, 0.25, 0.5, 0.75, 1.0], [0.0, 0.0, 0.0, 0.0, 0.0]]
    description: "Reservation-based THP allocator"

  reservethp_sweep2:
    name: "reservethp"
    file: "reservethp.cfg"
    sweeps:
      - identifier: ["frag"]
        option: ["--perf_model/reserve_thp/target_fragmentation="]
        values: [[0.0, 0.25, 0.5, 0.75, 1.0]]
      - identifier: ["promotionthresh"]
        option: ["--perf_model/reserve_thp/threshold_for_promotion="]
        values: [[0.0, 0.0, 0.0, 0.0, 0.0]]
    description: "Reservation-based THP allocator"

experiment_suites:
  reservethp:
    description: "ReserveTHP"
    configs: [no_translation, reservethp]
    trace_suite: all_vm
    instruction_count: 300000000
```
### Trace Suites 🧭

- `tracelist_base_path`: Base path to tracelist files, relative to `experiments/` directory
- `tracelists`: Explicit list of tracelists included in the trace suite

### Config Zoo 🧪

- `configs_base_path`: Base path to configs, relative to `experiments/` directory
- `configs.<name>.name`: Used in job names and result folder prefixes (e.g., `notrans_<trace>`)
- `configs.<name>.file`: Config file name, relative to `configs_base_path`
- `extends`: Optional list of option overrides appended to the base config. Example above (`reservethp-full-fragmentation`) appends `--perf_model/reserve_thp/target_fragmentation=0.0` to `reservethp.cfg` when building the command.
- `sweeps`: Parameter sweeping:
  - Multi-variable sweep (zipped): A single `sweeps` entry with multiple `identifier`s and `option`s. The `values` lists are zipped by index to create runs. Using `reservethp_sweep1` above, 5 runs are produced, pairing each `frag` value with the corresponding `promotionthresh` value (all zeros in this example).
  - Multi-entry sweep (matrix): Multiple `sweeps` entries create a Cartesian product across entries. In `reservethp_sweep2`, `frag` values and `promotionthresh` values are combined across entries. With both lists length 5, this yields up to 25 combinations; since `promotionthresh` is all zeros, it effectively yields 5 distinct runs.

### Experiment Suites 🧬

- `description`: Short summary of the suite.
- `configs`: List of configs (from the Config Zoo) to include in this suite.
- `trace_suite`: Name of the trace suite to use (from Trace Suites).
- `instruction_count`: Number of instructions for each run.

## 1) Create Experiments (`create_experiments.py`) 🚀

Generates a jobfile (`.sh`) of `sbatch` commands from `clist.yaml`.

Interface:

```bash
python3 experiments/create_experiments.py \
  --artifact-path /home/user/workspace/safari/vmorph/virtuoso_artifact \
  --yaml experiments/clist.yaml \
  --suite reservethp \
  --suite-dir-name reservethp \
  --force
```

 - Resolves tracelists and configs and builds `sbatch` commands using Sniper `run-sniper`.
 - Creates an experiment directory `experiments/exp_<suite>/` (e.g., `experiments/exp_reservethp/`) containing:
   - `trace_list.csv`, `config_list.csv`, `job_list.csv`
   - `jobfile.sh` with all sbatch commands
   - `results/` (output root for job runs)
   - `status/` (populated by the status script)
 - Names outputs as `<config>_<tracename>` under `experiments/exp_<suite>/results/`.
 - `--force`: Overwrites an existing `experiments/exp_<suite>/` directory and regenerates lists and `jobfile.sh`. Without `--force`, the script will refuse to overwrite.

## 2) Safe Submit (`safe_submit.py`) 🛡️

Validates environment and jobfile, then submits with SLURM throttling.

- File: [experiments/safe_submit.py](experiments/safe_submit.py)
- Key flags:
  - `--dry-run`: validate only
  - `--force`: submit despite validation errors
  - `--verbose`: detailed output
  - `--sniper-dir`: override Sniper root
  - `--max-slurm-jobs`: limit concurrent queue size (default 500)
  - `--slurm-retry-delay`: wait when queue at limit (default 60.0s)
  - `--slurm-submit-delay`: pause between submissions (default 0.1s)

Usage:

```bash
# Validate only
python3 experiments/safe_submit.py experiments/exp_reservethp/jobfile.sh --dry-run

# Submit with throttling
python3 experiments/safe_submit.py experiments/exp_reservethp/jobfile.sh \
  --max-slurm-jobs 400 \
  --slurm-retry-delay 60 \
  --slurm-submit-delay 0.1
```

Validations performed:
- Sniper compilation present and up-to-date (`run-sniper`, `lib/sniper` exist; source newer than build triggers a warning)
- Debug flags not enabled (checks `Makefile.config`, compiler flags, and `DEBUG` macros)
- Trace files exist (supports `.sift` fallback)
- Config files exist (absolute paths or relative to Sniper `config/`)
- Output directories sanity (existing with/without `sim.out`)
- Disk space on output location is sufficient
- SLURM queue check (job-name conflicts and queue size overview)

## 3) Monitor Status (`get_experiments_status.py`) 📊

Summarizes which outputs exist, which are completed (e.g., `sim.out` present), and which failed or are missing.

Interface:

```bash
python3 experiments/get_experiments_status.py \
  --exp-dir experiments/exp_reservethp
```

- Produces CSVs with traces that are: `done`, `running`, `error`, `roi_error`, `exception`, `pending` by tracename.

Statuses:
- `done`: `slurm.out`, `slurm.err`, and `simulation/sim.stats` all exist in the result directory.
- `running`: At least one of the log files exists but not all completion criteria are met; not classified as error/exception.
- `error`: `slurm.err` contains a Python traceback ("Traceback (most recent call last):").
- `roi_error`: `slurm.err` contains "ValueError: Invalid prefix roi-end".
- `exception`: `slurm.err` contains "Internal exception".
- `pending`: Neither `slurm.out` nor `slurm.err` exists yet.

## 4) Rerun Missing/Failed (`create_rerun_experiments.py`) 🔁

Creates a new jobfile with only those runs identified by status as `missing` or `failed`.

Interface:

```bash
python3 experiments/create_rerun_experiments.py \
  --exp-dir experiments/exp_reservethp \
  --status experiments/exp_reservethp/status/error.csv \
  --jobfile experiments/exp_reservethp/jobfile_rerun.sh
```

 

## Trace Lists (`.tlist`) 📂

- Each `.tlist` file format:
  - First non-empty line: base directory of traces.
  - Subsequent lines: trace specifications, typically `name, relative_path, L2TLB MPKI, Norm. PTW Latency`.
- Parser: [experiment_scripts/common/traces.py](experiment_scripts/common/traces.py)
- Note: The `clist.yaml` structure and `.tlist` parser here are reference implementations—adapt them to your environment and naming as needed.

## Quick Start ⚡

1. Write `experiments/clist.yaml` as shown above.
2. Generate a jobfile:

```bash
python3 experiments/create_experiments.py \
  --artifact-path /home/user/workspace/safari/vmorph/virtuoso_artifact \
  --clist experiments/clist.yaml \
  --suite reservethp \
  --suite-dir-name reservethp \
  --force
```

3. Validate and submit:

```bash
python3 experiments/safe_submit.py experiments/exp_reservethp/jobfile.sh \
  --max-slurm-jobs 500 --slurm-retry-delay 60 --slurm-submit-delay 0.1
```

4. Monitor:

```bash
python3 experiments/get_experiments_status.py \
  --exp-dir experiments/exp_reservethp
```

5. Rerun if needed:

```bash
python3 experiments/create_rerun_experiments.py \
  --exp-dir experiments/exp_reservethp \
  --status experiments/exp_reservethp/status/error.csv \
  --jobfile experiments/exp_reservethp/jobfile_rerun.sh

## Logs 📄

- `experiments/exp_reservethp/` (example):
  - `jobfile_reservethp.sh`: generated jobfile with sbatch lines
  - `status_reservethp.csv`: status report from `get_experiments_status.py`
  - `create_experiments.log`: jobfile generation diagnostics (warnings/skips)
  - `submit.log`: submission summary (optional; can capture `safe_submit.py` output)
- Results directories (e.g., `results/reservethp/<config>_<trace>/`):
  - `slurm.out`, `slurm.err`: SLURM logs per-job
  - `sim.out`: simulator output indicating completion
```

## Notes 📝

- Jobfile lines should be `sbatch` invocations wrapping `run-sniper` via `native_wrapper.sh`.
- Throttling in `safe_submit.py` uses SLURM queue size for `$USER`.
