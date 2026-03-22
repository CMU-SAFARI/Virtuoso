#!/usr/bin/env python3
"""
Multicore test runner for Sniper.

Runs multicore experiments defined in test_config.yaml. Each experiment
launches Sniper with:
  - -n <ncores> matching the topology
  - --traces=t0,t1,...,tN-1   (one trace per core)
  - -s stop-by-icount:<N>     (per-core instruction count)
  - --sim-end=last-restart     (restart finished traces until slowest finishes)
  - scheduler/type=static      (pin traces to cores)
  - general/num_host_cores=<ncores>  (use as many host threads as simulated cores)
  - topology overrides for total_cores and network model
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import yaml
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
from pathlib import Path

# ── Paths ────────────────────────────────────────────────────────────────────
SCRIPT_DIR = Path(__file__).parent
SNIPER_ROOT = SCRIPT_DIR.parent.parent  # testing/multicore -> sniper root
CONFIG_FILE = SCRIPT_DIR / "test_config.yaml"


# ── Config helpers ───────────────────────────────────────────────────────────
def load_config():
    """Load test configuration from YAML file."""
    with open(CONFIG_FILE, "r") as f:
        return yaml.safe_load(f)


def _path(config, key, default):
    """Resolve a path key from config['paths'], relative to SNIPER_ROOT if needed."""
    raw = config.get("paths", {}).get(key, default)
    if raw.startswith("./"):
        return SNIPER_ROOT / raw[2:]
    return Path(raw)


def get_results_dir(config):
    return _path(config, "results_dir", "./results/multicore_testing")


def get_configs_dir(config):
    return _path(config, "configs_dir", "./config/address_translation_schemes")


def get_traces_dir(config):
    return _path(config, "traces_dir", os.environ.get("VIRTUOSO_TRACES", "/path/to/traces"))


def get_config_path(config_name, config_def, config):
    """Get the absolute path to a .cfg file for a given config entry."""
    configs_dir = get_configs_dir(config)
    if "file" in config_def:
        return configs_dir / config_def["file"]
    if "config_file" in config_def:
        p = config_def["config_file"]
        return Path(p) if p.startswith("/") else SNIPER_ROOT / p
    return configs_dir / f"{config_name}.cfg"


# ── Trace resolution ────────────────────────────────────────────────────────
def resolve_traces_for_pool(pool_def, ncores, config):
    """
    Return a list of *ncores* absolute trace paths according to the pool.

    Homogeneous pools replicate one trace.
    Heterogeneous pools pick the first *ncores* entries, wrapping if the list
    is shorter than ncores.
    """
    traces_dir = get_traces_dir(config)
    pool_type = pool_def.get("type", "homogeneous")

    if pool_type == "homogeneous":
        trace = pool_def["trace"]
        full = traces_dir / trace
        return [str(full)] * ncores

    # heterogeneous
    raw = pool_def["traces"]
    paths = []
    for i in range(ncores):
        t = raw[i % len(raw)]
        paths.append(str(traces_dir / t))
    return paths


# ── Cleanup ──────────────────────────────────────────────────────────────────
def cleanup_old_results(base_name, results_dir):
    """Remove old timestamped result directories with the same base name."""
    if not results_dir.exists():
        return
    pattern = re.compile(rf"^{re.escape(base_name)}_\d{{8}}_\d{{6}}$")
    old_dirs = [d for d in results_dir.iterdir() if d.is_dir() and pattern.match(d.name)]
    if old_dirs:
        print(f"  Cleaning up {len(old_dirs)} old result dir(s) for '{base_name}'...")
        for d in old_dirs:
            try:
                shutil.rmtree(d)
                print(f"    Deleted: {d.name}")
            except Exception as e:
                print(f"    Warning: could not delete {d.name}: {e}")


# ── Single-test runner ───────────────────────────────────────────────────────
def run_single_test(
    test_name,
    config_name,
    config_def,
    topology_name,
    topology_def,
    trace_paths,
    results_dir,
    full_config,
    instructions,
    sim_end,
    dry_run=False,
):
    """
    Build and execute a single multicore Sniper invocation.

    Returns (success: bool, result_dir: Path).
    """
    ncores = topology_def["ncores"]

    # Result directory
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_name = f"{test_name}_{config_name}_{topology_name}"
    result_name = f"{base_name}_{timestamp}"
    result_dir = results_dir / result_name
    cleanup_old_results(base_name, results_dir)

    # Base command
    config_path = get_config_path(config_name, config_def, full_config)
    cmd = [
        str(SNIPER_ROOT / "run-sniper"),
        "-n", str(ncores),
        "-c", str(config_path),
        "-d", str(result_dir),
        "-s", f"stop-by-icount-percore:{instructions}",
        f"--sim-end={sim_end}",
    ]

    # Topology overrides (total_cores, network type, etc.)
    for override in topology_def.get("overrides", []):
        cmd.extend(["-g", f"--{override}"])

    # Static scheduler
    cmd.extend(["-g", "--scheduler/type=static"])

    # Use as many host threads as simulated cores
    cmd.extend(["-g", f"--general/num_host_cores={ncores}"])

    # Config-level overrides (extends)
    for ext in config_def.get("extends", []):
        if ext.startswith("--"):
            cmd.extend(["-g", ext])
        else:
            cmd.extend(["-g", f"--{ext}"])

    # Traces – comma-separated, one per core
    traces_arg = ",".join(trace_paths)
    cmd.append(f"--traces={traces_arg}")

    # ── Print summary ──
    print(f"\n{'=' * 70}")
    print(f"Test:     {test_name}")
    print(f"Config:   {config_name}  ({config_path})")
    print(f"Topology: {topology_name}  ({ncores} cores)")
    print(f"Traces:   {len(trace_paths)} traces")
    for i, tp in enumerate(trace_paths):
        print(f"  core {i}: {Path(tp).name}")
    print(f"Icount:   {instructions:,} per core")
    print(f"Sim-end:  {sim_end}")
    print(f"Result:   {result_dir}")
    print(f"Command:  {' '.join(cmd)}")
    print(f"{'=' * 70}")

    if dry_run:
        print("[DRY RUN] Would execute above command")
        return True, result_dir

    if not config_path.exists():
        print(f"ERROR: Config file not found: {config_path}")
        return False, result_dir

    timeout = full_config.get("defaults", {}).get("timeout_seconds", 7200)
    try:
        result = subprocess.run(
            cmd,
            cwd=str(SNIPER_ROOT),
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        if result.returncode != 0:
            print(f"FAILED (rc={result.returncode})")
            if result.stderr:
                print(f"STDERR:\n{result.stderr[:3000]}")
            return False, result_dir
        print("SUCCESS")
        return True, result_dir
    except subprocess.TimeoutExpired:
        print(f"TIMEOUT after {timeout}s")
        return False, result_dir
    except Exception as e:
        print(f"ERROR: {e}")
        return False, result_dir


# ── Suite runner ─────────────────────────────────────────────────────────────
def run_suite(suite_name, config, parallel=1, dry_run=False, instructions=None):
    """Run all (topology × config × trace_pool) combinations for a suite."""
    suites = config.get("suites", {})
    if suite_name not in suites:
        print(f"Error: unknown suite '{suite_name}'")
        print(f"Available: {list(suites.keys())}")
        return False

    suite = suites[suite_name]
    topologies_raw = suite.get("topology", "4core")
    if isinstance(topologies_raw, str):
        topologies_raw = [topologies_raw]

    configs_to_test = suite.get("configs", [])
    pool_name = suite.get("trace_pool", "rnd_homo")
    suite_instructions = instructions or suite.get(
        "instruction_count", config.get("defaults", {}).get("instruction_count", 10000000)
    )
    sim_end = suite.get("sim_end", config.get("defaults", {}).get("sim_end", "last-restart"))

    results_dir = get_results_dir(config)
    results_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n{'#' * 70}")
    print(f"Suite:        {suite_name}")
    print(f"Description:  {suite.get('description', 'N/A')}")
    print(f"Topologies:   {topologies_raw}")
    print(f"Configs:      {configs_to_test}")
    print(f"Trace pool:   {pool_name}")
    print(f"Instructions: {suite_instructions:,}")
    print(f"Sim-end:      {sim_end}")
    print(f"Parallel:     {parallel}")
    print(f"{'#' * 70}")

    # Build list of jobs
    jobs = []
    for topo_name in topologies_raw:
        if topo_name not in config.get("topologies", {}):
            print(f"Warning: unknown topology '{topo_name}', skipping")
            continue
        topo_def = config["topologies"][topo_name]
        ncores = topo_def["ncores"]

        pool_def = config.get("trace_pools", {}).get(pool_name)
        if pool_def is None:
            print(f"Error: unknown trace_pool '{pool_name}'")
            return False
        trace_paths = resolve_traces_for_pool(pool_def, ncores, config)

        for cfg_name in configs_to_test:
            if cfg_name not in config.get("configs", {}):
                print(f"Warning: unknown config '{cfg_name}', skipping")
                continue
            cfg_def = config["configs"][cfg_name]
            jobs.append((
                suite_name,
                cfg_name,
                cfg_def,
                topo_name,
                topo_def,
                trace_paths,
                results_dir,
                config,
                suite_instructions,
                sim_end,
                dry_run,
            ))

    if not jobs:
        print("No valid jobs to run!")
        return False

    # Execute
    results = []
    if parallel > 1 and len(jobs) > 1:
        with ThreadPoolExecutor(max_workers=parallel) as pool:
            futures = {pool.submit(run_single_test, *j): j for j in jobs}
            for fut in as_completed(futures):
                j = futures[fut]
                try:
                    ok, rdir = fut.result()
                    results.append((j[1], j[3], ok, rdir))
                except Exception as e:
                    print(f"Job {j[1]}/{j[3]} failed: {e}")
                    results.append((j[1], j[3], False, None))
    else:
        for j in jobs:
            ok, rdir = run_single_test(*j)
            results.append((j[1], j[3], ok, rdir))

    # Summary
    print(f"\n{'=' * 70}")
    print(f"SUITE SUMMARY: {suite_name}")
    print(f"{'=' * 70}")
    passed = sum(1 for *_, ok, _ in results if ok)
    failed = len(results) - passed
    for cfg, topo, ok, rdir in results:
        status = "PASS" if ok else "FAIL"
        print(f"  [{status}] {cfg} @ {topo}")
    print(f"\nTotal: {passed} passed, {failed} failed out of {len(results)}")
    return failed == 0


# ── Listing helpers ──────────────────────────────────────────────────────────
def list_suites(config):
    print("\nAvailable multicore test suites:")
    print("-" * 70)
    for name, sdef in config.get("suites", {}).items():
        topo = sdef.get("topology", "?")
        cfgs = sdef.get("configs", [])
        pool = sdef.get("trace_pool", "?")
        instr = sdef.get("instruction_count", config.get("defaults", {}).get("instruction_count", "?"))
        print(f"  {name}:")
        print(f"    Description: {sdef.get('description', 'N/A')}")
        print(f"    Topology:    {topo}")
        print(f"    Configs:     {', '.join(cfgs[:6])}{'...' if len(cfgs) > 6 else ''}")
        print(f"    Trace pool:  {pool}")
        print(f"    Instructions: {instr:,}" if isinstance(instr, int) else f"    Instructions: {instr}")
        print()


def list_configs(config):
    print("\nAvailable configurations:")
    print("-" * 70)
    for name, cdef in config.get("configs", {}).items():
        f = cdef.get("file", cdef.get("config_file", f"{name}.cfg"))
        print(f"  {name}: {f}  — {cdef.get('description', '')}")
    print()


def list_topologies(config):
    print("\nAvailable topologies:")
    print("-" * 70)
    for name, tdef in config.get("topologies", {}).items():
        print(f"  {name}: {tdef['ncores']} cores — {tdef.get('description', '')}")
    print()


def list_trace_pools(config):
    print("\nAvailable trace pools:")
    print("-" * 70)
    for name, pdef in config.get("trace_pools", {}).items():
        ptype = pdef.get("type", "?")
        if ptype == "homogeneous":
            print(f"  {name} (homo): {pdef.get('trace', '?')}  — {pdef.get('description', '')}")
        else:
            traces = pdef.get("traces", [])
            print(f"  {name} (hetero): {len(traces)} traces  — {pdef.get('description', '')}")
    print()


# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Run Sniper multicore tests (per-core stop-by-icount, static scheduler)."
    )
    parser.add_argument("--suite", "-s", help="Test suite to run")
    parser.add_argument("--parallel", "-p", type=int, default=1, help="Number of parallel test jobs")
    parser.add_argument("--dry-run", "-n", action="store_true", help="Print commands without executing")
    parser.add_argument("--instructions", "-i", type=int, help="Override per-core instruction count")
    parser.add_argument("--list-suites", action="store_true", help="List available suites")
    parser.add_argument("--list-configs", action="store_true", help="List available configs")
    parser.add_argument("--list-topologies", action="store_true", help="List available topologies")
    parser.add_argument("--list-pools", action="store_true", help="List available trace pools")

    # Ad-hoc single-run mode
    parser.add_argument("--config", "-c", help="Single config name (ad-hoc run)")
    parser.add_argument("--topology", "-t", default="4core", help="Topology for ad-hoc run (default: 4core)")
    parser.add_argument("--trace-pool", default="rnd_homo", help="Trace pool for ad-hoc run (default: rnd_homo)")

    args = parser.parse_args()
    config = load_config()

    if args.list_suites:
        list_suites(config)
        return 0
    if args.list_configs:
        list_configs(config)
        return 0
    if args.list_topologies:
        list_topologies(config)
        return 0
    if args.list_pools:
        list_trace_pools(config)
        return 0

    if args.suite:
        ok = run_suite(args.suite, config, args.parallel, args.dry_run, args.instructions)
        return 0 if ok else 1

    if args.config:
        # Ad-hoc single run
        if args.config not in config.get("configs", {}):
            print(f"Error: unknown config '{args.config}'")
            return 1
        topo_name = args.topology
        if topo_name not in config.get("topologies", {}):
            print(f"Error: unknown topology '{topo_name}'")
            return 1
        pool_name = args.trace_pool
        pool_def = config.get("trace_pools", {}).get(pool_name)
        if pool_def is None:
            print(f"Error: unknown trace pool '{pool_name}'")
            return 1

        topo_def = config["topologies"][topo_name]
        ncores = topo_def["ncores"]
        trace_paths = resolve_traces_for_pool(pool_def, ncores, config)

        results_dir = get_results_dir(config)
        results_dir.mkdir(parents=True, exist_ok=True)

        instr = args.instructions or config.get("defaults", {}).get("instruction_count", 10000000)
        sim_end = config.get("defaults", {}).get("sim_end", "last-restart")

        ok, _ = run_single_test(
            "adhoc",
            args.config,
            config["configs"][args.config],
            topo_name,
            topo_def,
            trace_paths,
            results_dir,
            config,
            instr,
            sim_end,
            args.dry_run,
        )
        return 0 if ok else 1

    parser.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
