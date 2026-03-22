#!/usr/bin/env python3
"""
Test runner for Sniper MMU configurations.
Runs tests defined in test_config.yaml and collects results.
"""

import argparse
import subprocess
import yaml
import os
import sys
import shutil
import glob
import re
from pathlib import Path
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor, as_completed

# Paths
SCRIPT_DIR = Path(__file__).parent
SNIPER_ROOT = SCRIPT_DIR.parent
CONFIG_FILE = SCRIPT_DIR / "test_config.yaml"


def load_config():
    """Load test configuration from YAML file."""
    with open(CONFIG_FILE, 'r') as f:
        return yaml.safe_load(f)


def get_results_dir(config):
    """Get the results directory from config or use default."""
    if 'paths' in config and 'results_dir' in config['paths']:
        results_path = config['paths']['results_dir']
        if results_path.startswith('./'):
            return SNIPER_ROOT / results_path[2:]
        return Path(results_path)
    return SNIPER_ROOT / "results" / "testing"


def get_configs_dir(config):
    """Get the configs directory from config or use default."""
    if 'paths' in config and 'configs_dir' in config['paths']:
        configs_path = config['paths']['configs_dir']
        if configs_path.startswith('./'):
            return SNIPER_ROOT / configs_path[2:]
        return Path(configs_path)
    return SNIPER_ROOT / "config"


def get_traces_dir(config):
    """Get the traces directory from config or use default."""
    if 'paths' in config and 'traces_dir' in config['paths']:
        return Path(config['paths']['traces_dir'])
    return Path(os.environ.get("VIRTUOSO_TRACES", "/path/to/traces"))


def get_config_path(config_name, config_def, config):
    """Get the path to a configuration file."""
    configs_dir = get_configs_dir(config)
    
    # New format: 'file' key
    if 'file' in config_def:
        return configs_dir / config_def['file']
    # Old format: 'config_file' key (absolute or relative path)
    if 'config_file' in config_def:
        cfg_path = config_def['config_file']
        if cfg_path.startswith('/'):
            return Path(cfg_path)
        return SNIPER_ROOT / cfg_path
    # Default: assume config name matches file name
    return configs_dir / f"{config_name}.cfg"


def cleanup_old_results(base_name, results_dir):
    """
    Remove old result directories with the same base name but different timestamps.
    Keeps only the most recent one (which is about to be created).
    """
    if not results_dir.exists():
        return
    
    # Pattern: base_name_YYYYMMDD_HHMMSS
    pattern = re.compile(rf'^{re.escape(base_name)}_\d{{8}}_\d{{6}}$')
    
    old_dirs = []
    for item in results_dir.iterdir():
        if item.is_dir() and pattern.match(item.name):
            old_dirs.append(item)
    
    if old_dirs:
        print(f"  Cleaning up {len(old_dirs)} old result director{'ies' if len(old_dirs) > 1 else 'y'} for '{base_name}'...")
        for old_dir in old_dirs:
            try:
                shutil.rmtree(old_dir)
                print(f"    Deleted: {old_dir.name}")
            except Exception as e:
                print(f"    Warning: Could not delete {old_dir.name}: {e}")


def resolve_trace_path(trace_name, suite_def, config):
    """Resolve a trace name to a full path."""
    # Check if suite has a custom trace base dir
    if 'trace_base_dir' in suite_def:
        base_dir = Path(suite_def['trace_base_dir'])
    else:
        base_dir = get_traces_dir(config)
    
    trace_path = base_dir / trace_name
    
    # If not found, try without extension variations
    if not trace_path.exists():
        # Try common extensions
        for ext in ['', '.sift', '.champsim.gz', '.gz']:
            test_path = base_dir / f"{trace_name}{ext}"
            if test_path.exists():
                return str(test_path)
    
    return str(trace_path)


def run_single_test(test_name, config_name, config_def, trace_path, results_dir, 
                    full_config, instructions=100000000, extra_args=None, dry_run=False):
    """Run a single test configuration."""
    
    # Create unique result directory name
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_name = f"{test_name}_{config_name}"
    result_name = f"{base_name}_{timestamp}"
    result_dir = results_dir / result_name
    
    # Clean up old results with the same base name
    cleanup_old_results(base_name, results_dir)
    
    # Build command
    config_path = get_config_path(config_name, config_def, full_config)
    
    cmd = [
        str(SNIPER_ROOT / "run-sniper"),
        "-c", str(config_path),
        "-d", str(result_dir),
        "-n", "1",
        "-s", f"stop-by-icount:{instructions}",
    ]
    
    # Add any extra arguments from config
    if extra_args:
        cmd.extend(extra_args)
    
    # Add config overrides if specified (support both 'extends' and 'overrides')
    extends = config_def.get('extends', []) or config_def.get('overrides', [])
    if extends:
        for override in extends:
            # Handle both "--key=value" and "key=value" formats
            if override.startswith('--'):
                cmd.extend(["-g", override[2:]])  # Remove leading --
            else:
                cmd.extend(["-g", override])
    
    # Add traces at the end (must be --traces=path format with no space)
    cmd.append(f"--traces={trace_path}")
    
    print(f"\n{'='*60}")
    print(f"Test: {test_name} | Config: {config_name}")
    print(f"Config file: {config_path}")
    print(f"Trace: {trace_path}")
    print(f"Result dir: {result_dir}")
    print(f"Command: {' '.join(cmd)}")
    print(f"{'='*60}")
    
    if dry_run:
        print("[DRY RUN] Would execute above command")
        return True, result_dir
    
    # Check if config file exists
    if not config_path.exists():
        print(f"ERROR: Config file not found: {config_path}")
        return False, result_dir
    
    # Run the test
    try:
        result = subprocess.run(
            cmd,
            cwd=str(SNIPER_ROOT),
            capture_output=True,
            text=True,
            timeout=3600  # 1 hour timeout
        )
        
        if result.returncode != 0:
            print(f"FAILED with return code {result.returncode}")
            print(f"STDERR: {result.stderr[:2000] if result.stderr else 'None'}")
            return False, result_dir
        
        print(f"SUCCESS")
        return True, result_dir
        
    except subprocess.TimeoutExpired:
        print(f"TIMEOUT after 1 hour")
        return False, result_dir
    except Exception as e:
        print(f"ERROR: {e}")
        return False, result_dir


def run_test_parallel(args):
    """Wrapper for parallel execution."""
    return run_single_test(*args)


def run_suite(suite_name, config, parallel=1, dry_run=False, instructions=None):
    """Run a test suite."""
    
    # Support both 'suites' and 'test_suites' keys
    suites = config.get('suites', config.get('test_suites', {}))
    
    if suite_name not in suites:
        print(f"Error: Unknown test suite '{suite_name}'")
        print(f"Available suites: {list(suites.keys())}")
        return False
    
    suite = suites[suite_name]
    configs_to_test = suite.get('configs', [])
    traces = suite.get('traces', ['rnd.sift'])  # Default trace
    suite_instructions = suite.get('instruction_count', suite.get('instructions', 100000000))
    
    # Command line instructions override suite default
    if instructions:
        suite_instructions = instructions
    
    # Get results directory
    results_dir = get_results_dir(config)
    results_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"\n{'#'*60}")
    print(f"Running test suite: {suite_name}")
    print(f"Description: {suite.get('description', 'No description')}")
    print(f"Configs: {configs_to_test}")
    print(f"Traces: {traces}")
    print(f"Instructions: {suite_instructions:,}")
    print(f"Parallel: {parallel}")
    print(f"{'#'*60}")
    
    # Prepare test jobs - one per (config, trace) combination
    jobs = []
    for config_name in configs_to_test:
        if config_name not in config.get('configs', {}):
            print(f"Warning: Unknown config '{config_name}', skipping")
            continue
        
        config_def = config['configs'][config_name]
        
        for trace_name in traces:
            trace_path = resolve_trace_path(trace_name, suite, config)
            
            # Create a unique test name for config+trace combination
            trace_short = Path(trace_name).stem.replace('.champsim', '').replace('.sift', '')
            test_name = f"{suite_name}_{trace_short}"
            
            jobs.append((
                test_name,
                config_name,
                config_def,
                trace_path,
                results_dir,
                config,  # Pass full config for path resolution
                suite_instructions,
                None,  # extra_args
                dry_run
            ))
    
    if not jobs:
        print("No valid jobs to run!")
        return False
    
    # Run tests
    results = []
    if parallel > 1 and len(jobs) > 1:
        with ThreadPoolExecutor(max_workers=parallel) as executor:
            futures = {executor.submit(run_test_parallel, job): job for job in jobs}
            for future in as_completed(futures):
                job = futures[future]
                try:
                    success, result_dir = future.result()
                    results.append((job[1], job[3], success, result_dir))  # config_name, trace, success, dir
                except Exception as e:
                    print(f"Job {job[1]} failed with exception: {e}")
                    results.append((job[1], job[3], False, None))
    else:
        for job in jobs:
            success, result_dir = run_single_test(*job)
            results.append((job[1], job[3], success, result_dir))
    
    # Summary
    print(f"\n{'='*60}")
    print(f"SUITE SUMMARY: {suite_name}")
    print(f"{'='*60}")
    
    passed = sum(1 for _, _, success, _ in results if success)
    failed = len(results) - passed
    
    for config_name, trace, success, result_dir in results:
        status = "PASS" if success else "FAIL"
        trace_short = Path(trace).name[:30]
        print(f"  [{status}] {config_name} @ {trace_short}")
    
    print(f"\nTotal: {passed} passed, {failed} failed")
    
    return failed == 0


def list_suites(config):
    """List available test suites."""
    suites = config.get('suites', config.get('test_suites', {}))
    
    print("\nAvailable test suites:")
    print("-" * 60)
    for suite_name, suite_def in suites.items():
        configs = suite_def.get('configs', [])
        traces = suite_def.get('traces', ['default'])
        desc = suite_def.get('description', 'No description')
        instr = suite_def.get('instruction_count', suite_def.get('instructions', 100000000))
        print(f"  {suite_name}:")
        print(f"    Description: {desc}")
        print(f"    Configs: {', '.join(configs[:5])}{'...' if len(configs) > 5 else ''}")
        print(f"    Traces: {len(traces)} trace(s)")
        print(f"    Instructions: {instr:,}")
        print()


def list_configs(config):
    """List available configurations."""
    print("\nAvailable configurations:")
    print("-" * 60)
    for config_name, config_def in config.get('configs', {}).items():
        desc = config_def.get('description', 'No description')
        cfg_file = config_def.get('file', config_def.get('config_file', f'{config_name}.cfg'))
        print(f"  {config_name}:")
        print(f"    File: {cfg_file}")
        print(f"    Description: {desc}")
        if 'extends' in config_def:
            print(f"    Extends: {len(config_def['extends'])} override(s)")
        print()


def main():
    parser = argparse.ArgumentParser(description="Run Sniper MMU tests")
    parser.add_argument('--suite', '-s', help="Test suite to run")
    parser.add_argument('--config', '-c', help="Single config to test")
    parser.add_argument('--trace', '-t', help="Trace file to use")
    parser.add_argument('--parallel', '-p', type=int, default=1, 
                        help="Number of parallel tests")
    parser.add_argument('--dry-run', '-n', action='store_true',
                        help="Show commands without running")
    parser.add_argument('--instructions', '-i', type=int,
                        help="Number of instructions to simulate")
    parser.add_argument('--list-suites', action='store_true',
                        help="List available test suites")
    parser.add_argument('--list-configs', action='store_true',
                        help="List available configurations")
    
    args = parser.parse_args()
    
    # Load configuration
    config = load_config()
    
    if args.list_suites:
        list_suites(config)
        return 0
    
    if args.list_configs:
        list_configs(config)
        return 0
    
    if args.suite:
        success = run_suite(args.suite, config, args.parallel, args.dry_run, args.instructions)
        return 0 if success else 1
    
    if args.config:
        # Run single config
        if args.config not in config.get('configs', {}):
            print(f"Error: Unknown config '{args.config}'")
            return 1
        
        trace_path = args.trace
        if not trace_path:
            traces_dir = get_traces_dir(config)
            trace_path = str(traces_dir / "rnd.sift")
        
        results_dir = get_results_dir(config)
        results_dir.mkdir(parents=True, exist_ok=True)
        
        success, _ = run_single_test(
            "manual",
            args.config,
            config['configs'][args.config],
            trace_path,
            results_dir,
            config,
            args.instructions or 100000000,
            dry_run=args.dry_run
        )
        return 0 if success else 1
    
    parser.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
