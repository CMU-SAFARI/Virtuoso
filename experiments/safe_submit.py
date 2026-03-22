#!/usr/bin/env python3
"""
Safe Submit Script for Cluster Job Submission

This script performs comprehensive safety checks before submitting jobs to the cluster.
It validates:
1. Sniper compilation is up-to-date
2. Debug flags are disabled in release builds
3. All required directories exist
4. Trace files exist
5. Config files exist
6. Disk space is sufficient
7. No conflicting jobs are running

Usage:
    python safe_submit.py <jobfile> [--dry-run] [--force] [--verbose]
"""

import argparse
import os
import subprocess
import sys
import re
import shutil
import time
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple
from common.printutils import Colors, print_header
SLURM_RETRY_DELAY = 1 * 60
SLURM_SUBMIT_DELAY = 0.1



@dataclass
class Config:
    """Configuration for the safe submit script."""
    sniper_dir: str = ""
    debug_config: str = ""
    
    def __post_init__(self):
        # Try to find sniper directory
        script_dir = Path(__file__).parent.absolute()
        
        # Check common locations relative to experiment_scripts
        possible_paths = [
            script_dir.parent / "simulator" / "sniper",
            script_dir.parent / "new_infra" / "simulator" / "sniper",
            Path(os.environ.get("VIRTUOSO_ROOT", "")) / "simulator" / "sniper",
        ]
        
        for path in possible_paths:
            if path.exists() and (path / "Makefile").exists():
                self.sniper_dir = str(path)
                break
        
        if not self.sniper_dir:
            # Try to find from environment
            self.sniper_dir = os.environ.get("SNIPER_ROOT", "")
        
        # Set debug config path
        if self.sniper_dir:
            self.debug_config = os.path.join(self.sniper_dir, "config", "debug.cfg")


@dataclass
class JobSpec:
    """Represents a single job specification."""
    name: str
    trace_file: str
    config_file: str
    output_dir: str
    instructions: int = 0
    extra_args: List[str] = field(default_factory=list)
    line_number: int = 0


@dataclass
class ValidationResult:
    """Result of a validation check."""
    passed: bool
    message: str
    severity: str = "error"  # error, warning, info
    details: Optional[str] = None


 


def print_check(name: str, result: ValidationResult):
    """Print a check result."""
    if result.passed:
        status = f"{Colors.GREEN}✓ PASS{Colors.RESET}"
    elif result.severity == "warning":
        status = f"{Colors.YELLOW}⚠ WARN{Colors.RESET}"
    else:
        status = f"{Colors.RED}✗ FAIL{Colors.RESET}"
    
    print(f"  {status}  {name}: {result.message}")
    if result.details:
        for line in result.details.split('\n'):
            print(f"         {Colors.WHITE}{line}{Colors.RESET}")


def parse_jobfile(jobfile_path: str, config: Config) -> Tuple[List[JobSpec], List[str]]:
    """
    Parse a jobfile and extract job specifications.
    
    Returns:
        Tuple of (list of JobSpec, list of parse errors)
    """
    jobs = []
    errors = []
    
    if not os.path.exists(jobfile_path):
        errors.append(f"Jobfile not found: {jobfile_path}")
        return jobs, errors
    
    with open(jobfile_path, 'r') as f:
        lines = f.readlines()
    
    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        
        # Skip empty lines and comments
        if not line or line.startswith('#'):
            continue
        
        # Parse sbatch command or native_wrapper.sh command
        job = parse_job_line(line, line_num, config)
        if job:
            jobs.append(job)
        elif 'sbatch' in line or 'native_wrapper' in line or 'run-sniper' in line:
            errors.append(f"Line {line_num}: Could not parse job specification")
    
    return jobs, errors


def parse_job_line(line: str, line_num: int, config: Config) -> Optional[JobSpec]:
    """Parse a single job line and extract the job specification."""
    job = JobSpec(name="", trace_file="", config_file="", output_dir="", line_number=line_num)
    
    # Extract job name from sbatch --job-name or -J
    name_match = re.search(r'(?:--job-name[=\s]|--?J\s*)([^\s]+)', line)
    if name_match:
        job.name = name_match.group(1)
    
    # Extract the run-sniper command part to avoid matching sbatch arguments
    run_sniper_match = re.search(r'run-sniper\s+(.*)', line)
    if run_sniper_match:
        sniper_args = run_sniper_match.group(1)
        # Remove trailing quote if present
        if sniper_args.endswith('"'):
            sniper_args = sniper_args[:-1]
    else:
        sniper_args = line

    # Extract trace file (-t or --traces)
    trace_match = re.search(r'(?:-t\s+|--traces[=\s])([^\s]+)', sniper_args)
    if trace_match:
        job.trace_file = trace_match.group(1).strip('"\'')
    
    # Extract config file (-c or --config)
    config_match = re.search(r'(?:-c\s+|--config[=\s])([^\s]+)', sniper_args)
    if config_match:
        job.config_file = config_match.group(1).strip('"\'')
    
    # Extract output directory (-d or --output-dir or -o)
    output_match = re.search(r'(?:-d\s+|-o\s+|--output[=\s]|--output-dir[=\s])([^\s]+)', sniper_args)
    if output_match:
        job.output_dir = output_match.group(1).strip('"\'')
    
    # Extract instruction count (-s stop-by-icount-percore:)
    instr_match = re.search(r'-s\s+stop-by-icount-percore:(\d+[KMG]?)', sniper_args, re.IGNORECASE)
    if instr_match:
        job.instructions = parse_instruction_count(instr_match.group(1))
    
    # Only return if we have essential fields
    if job.trace_file or job.config_file:
        if not job.name:
            job.name = f"job_line_{line_num}"
        return job
    
    return None


def parse_instruction_count(count_str: str) -> int:
    """Parse instruction count with K/M/G suffixes."""
    count_str = count_str.upper()
    multipliers = {'K': 1000, 'M': 1000000, 'G': 1000000000}
    
    for suffix, mult in multipliers.items():
        if count_str.endswith(suffix):
            return int(float(count_str[:-1]) * mult)
    
    return int(count_str)


def check_sniper_compilation(config: Config, verbose: bool = False) -> ValidationResult:
    """Check if sniper is compiled and up-to-date."""
    sniper_dir = config.sniper_dir
    
    if not sniper_dir or not os.path.exists(sniper_dir):
        return ValidationResult(
            False, 
            "Sniper directory not found",
            details=f"Expected at: {sniper_dir}"
        )
    
    # Check if run-sniper exists
    run_sniper = os.path.join(sniper_dir, "run-sniper")
    if not os.path.exists(run_sniper):
        return ValidationResult(
            False,
            "run-sniper not found - sniper may not be compiled",
            details=f"Expected at: {run_sniper}"
        )
    
    # Check for lib/sniper
    sniper_lib = os.path.join(sniper_dir, "lib", "sniper")
    if not os.path.exists(sniper_lib):
        return ValidationResult(
            False,
            "Sniper library not found - compilation may be incomplete",
            details=f"Expected at: {sniper_lib}"
        )
    
    # Check if source files are newer than compiled files
    makefile = os.path.join(sniper_dir, "Makefile")
    if os.path.exists(makefile) and os.path.exists(sniper_lib):
        # Get latest source modification time
        source_dirs = ["common", "standalone", "sift", "frontend"]
        latest_source = 0
        
        for src_dir in source_dirs:
            src_path = os.path.join(sniper_dir, src_dir)
            if os.path.exists(src_path):
                for root, dirs, files in os.walk(src_path):
                    for f in files:
                        if f.endswith(('.cc', '.h', '.cpp', '.hpp')):
                            fpath = os.path.join(root, f)
                            if not os.path.exists(fpath):
                                continue
                            mtime = os.path.getmtime(fpath)
                            if mtime > latest_source:
                                latest_source = mtime
        
        lib_mtime = os.path.getmtime(sniper_lib)
        
        if latest_source > lib_mtime:
            return ValidationResult(
                False,
                "Source files modified after compilation",
                severity="warning",
                details="Run 'make' to recompile sniper"
            )
    
    return ValidationResult(True, "Sniper is compiled and up-to-date")


def check_debug_flags(config: Config, verbose: bool = False) -> ValidationResult:
    """Check if debug flags are disabled."""
    sniper_dir = config.sniper_dir
    debug_config = config.debug_config
    
    if not sniper_dir:
        return ValidationResult(False, "Sniper directory not set")
    
    issues = []
    
    # Check Makefile.config for DEBUG flags
    makefile_config = os.path.join(sniper_dir, "Makefile.config")
    if os.path.exists(makefile_config):
        with open(makefile_config, 'r') as f:
            content = f.read()
            
            # Check for DEBUG=1 or similar
            if re.search(r'^\s*DEBUG\s*[?:]?=\s*1', content, re.MULTILINE):
                issues.append("DEBUG=1 found in Makefile.config")
            
            # Check for -g without -O
            if re.search(r'-g\b', content) and not re.search(r'-O[123s]', content):
                issues.append("Debug symbols (-g) enabled without optimization")
    
    # Check for debug.cfg being included in any config
    if debug_config and os.path.exists(debug_config):
        # This is just informational - debug.cfg existing is fine
        pass
    
    # Check for assert macros enabled
    common_makefile = os.path.join(sniper_dir, "common", "Makefile")
    if os.path.exists(common_makefile):
        with open(common_makefile, 'r') as f:
            content = f.read()
            if re.search(r'^\s*CFLAGS.*-DDEBUG', content, re.MULTILINE):
                issues.append("DEBUG macro defined in CFLAGS")
    
    if issues:
        return ValidationResult(
            False,
            "Debug flags detected",
            severity="warning",
            details='\n'.join(issues)
        )
    
    return ValidationResult(True, "No debug flags detected")


def check_trace_files(jobs: List[JobSpec], verbose: bool = False) -> ValidationResult:
    """Check if all trace files exist."""
    missing = []
    found = 0
    
    for job in jobs:
        if job.trace_file:
            # Handle comma-separated traces
            traces = job.trace_file.split(',')
            for trace in traces:
                trace = trace.strip()
                if trace and not os.path.exists(trace):
                    # Try with .sift extension
                    if not os.path.exists(trace + ".sift"):
                        missing.append(f"  Line {job.line_number}: {trace}")
                    else:
                        found += 1
                else:
                    found += 1
    
    if missing:
        return ValidationResult(
            False,
            f"Missing trace files ({len(missing)} missing, {found} found)",
            details='\n'.join(missing[:10]) + ('\n  ...' if len(missing) > 10 else '')
        )
    
    return ValidationResult(True, f"All {found} trace file(s) found")


def check_config_files(jobs: List[JobSpec], config: Config, verbose: bool = False) -> ValidationResult:
    """Check if all config files exist."""
    missing = []
    found = 0
    sniper_dir = config.sniper_dir
    
    for job in jobs:
        if job.config_file:
            config_path = job.config_file
            
            # Try absolute path first
            if os.path.exists(config_path):
                found += 1
                continue
            
            # Try relative to sniper config directory
            if sniper_dir:
                sniper_config = os.path.join(sniper_dir, "config", config_path)
                if os.path.exists(sniper_config):
                    found += 1
                    continue
                
                # Try with .cfg extension
                if os.path.exists(sniper_config + ".cfg"):
                    found += 1
                    continue
            
            missing.append(f"  Line {job.line_number}: {config_path}")
    
    if missing:
        return ValidationResult(
            False,
            f"Missing config files ({len(missing)} missing, {found} found)",
            details='\n'.join(missing[:10]) + ('\n  ...' if len(missing) > 10 else '')
        )
    
    return ValidationResult(True, f"All {found} config file(s) found")


def check_output_directories(jobs: List[JobSpec], verbose: bool = False) -> ValidationResult:
    """Check output directory status."""
    existing = []
    will_create = []
    
    for job in jobs:
        if job.output_dir:
            if os.path.exists(job.output_dir):
                # Check if it has results
                sim_out = os.path.join(job.output_dir, "sim.out")
                if os.path.exists(sim_out):
                    existing.append(f"  Line {job.line_number}: {job.output_dir} (has sim.out)")
                else:
                    existing.append(f"  Line {job.line_number}: {job.output_dir} (empty)")
            else:
                will_create.append(job.output_dir)
    
    if existing:
        return ValidationResult(
            False,
            f"Output directories already exist ({len(existing)})",
            severity="warning",
            details='\n'.join(existing[:10]) + ('\n  ...' if len(existing) > 10 else '')
        )
    
    return ValidationResult(True, f"{len(will_create)} output directories will be created")


def check_disk_space(jobs: List[JobSpec], verbose: bool = False) -> ValidationResult:
    """Check if there's enough disk space."""
    # Estimate space needed per job (configurable)
    SPACE_PER_JOB_MB = 5  # Conservative estimate
    
    total_needed_mb = len(jobs) * SPACE_PER_JOB_MB
    
    # Check disk space on likely output locations
    check_paths = set()
    for job in jobs:
        if job.output_dir:
            # Get the mount point
            path = job.output_dir
            while not os.path.exists(path) and path != '/':
                path = os.path.dirname(path)
            if os.path.exists(path):
                check_paths.add(path)
    
    if not check_paths:
        check_paths.add(os.getcwd())
    
    min_free_mb = float('inf')
    min_path = ""
    
    for path in check_paths:
        try:
            stat = os.statvfs(path)
            free_mb = (stat.f_bavail * stat.f_frsize) / (1024 * 1024)
            if free_mb < min_free_mb:
                min_free_mb = free_mb
                min_path = path
        except:
            continue
    
    if min_free_mb < total_needed_mb:
        return ValidationResult(
            False,
            f"Low disk space on {min_path}",
            details=f"Need ~{total_needed_mb/1024:.1f}GB, only {min_free_mb/1024:.1f}GB available"
        )
    
    if min_free_mb < total_needed_mb * 2:
        return ValidationResult(
            True,
            f"Disk space OK but limited ({min_free_mb/1024:.1f}GB free)",
            severity="warning"
        )
    
    return ValidationResult(True, f"Sufficient disk space ({min_free_mb/1024:.1f}GB free)")


def check_slurm_queue(jobs: List[JobSpec], verbose: bool = False) -> ValidationResult:
    """Check SLURM queue for conflicts."""
    try:
        result = subprocess.run(
            ['squeue', '-u', os.environ.get('USER', ''), '-h', '-o', '%j'],
            capture_output=True,
            text=True,
            timeout=10
        )
        
        if result.returncode != 0:
            return ValidationResult(True, "Could not check SLURM queue", severity="warning")
        
        running_jobs = set(result.stdout.strip().split('\n'))
        conflicts = []
        
        for job in jobs:
            if job.name in running_jobs:
                conflicts.append(f"  {job.name}")
        
        if conflicts:
            return ValidationResult(
                False,
                f"Jobs with same name already running ({len(conflicts)})",
                severity="warning",
                details='\n'.join(conflicts[:10])
            )
        
        queue_size = len([j for j in running_jobs if j])
        return ValidationResult(True, f"No conflicts (you have {queue_size} jobs in queue)")
        
    except FileNotFoundError:
        return ValidationResult(True, "SLURM not available", severity="info")
    except subprocess.TimeoutExpired:
        return ValidationResult(True, "SLURM check timed out", severity="warning")
    except Exception as e:
        return ValidationResult(True, f"Could not check SLURM: {e}", severity="warning")


def check_nfs_nodes(jobs: List[JobSpec], verbose: bool = False) -> ValidationResult:
    """Check NFS mount accessibility on cluster nodes."""
    try:
        # Get list of available nodes
        result = subprocess.run(
            ['sinfo', '-N', '-h', '-o', '%N %T'],
            capture_output=True,
            text=True,
            timeout=10
        )
        
        if result.returncode != 0:
            return ValidationResult(True, "Could not get node list", severity="warning")
        
        # Parse nodes and their states
        nodes = []
        for line in result.stdout.strip().split('\n'):
            parts = line.split()
            if len(parts) >= 2:
                node, state = parts[0], parts[1]
                if 'down' not in state and 'drain' not in state:
                    nodes.append(node)
        
        nodes = list(set(nodes))[:5]  # Check up to 5 nodes
        
        if not nodes:
            return ValidationResult(True, "No available nodes to check", severity="warning")
        
        # Get a path to check (first trace file)
        check_path = os.environ.get("STORAGE_ROOT", "/mnt/panzer")
        for job in jobs:
            if job.trace_file and os.path.dirname(job.trace_file):
                check_path = os.path.dirname(job.trace_file)
                break
        
        failed_nodes = []
        checked = 0
        
        for node in nodes:
            try:
                check_result = subprocess.run(
                    ['srun', '-N1', '-n1', '-w', node, '--quiet', 
                     'bash', '-c', f"test -d '{check_path}' && echo OK || echo FAIL"],
                    capture_output=True,
                    text=True,
                    timeout=15
                )
                checked += 1
                if 'OK' not in check_result.stdout:
                    failed_nodes.append(node)
            except subprocess.TimeoutExpired:
                failed_nodes.append(f"{node} (timeout)")
            except Exception:
                continue
        
        if failed_nodes:
            return ValidationResult(
                False,
                f"NFS not accessible on {len(failed_nodes)} node(s)",
                severity="warning",
                details=f"Failed: {', '.join(failed_nodes)}\nAdd: #SBATCH --exclude={','.join(n.split()[0] for n in failed_nodes)}"
            )
        
        return ValidationResult(True, f"NFS accessible on {checked} sampled nodes")
        
    except FileNotFoundError:
        return ValidationResult(True, "SLURM not available for NFS check", severity="info")
    except Exception as e:
        return ValidationResult(True, f"NFS check error: {e}", severity="warning")


def check_job_sanity(jobs: List[JobSpec], verbose: bool = False) -> ValidationResult:
    """Perform sanity checks on job specifications."""
    issues = []
    
    for job in jobs:
        # Check for very large instruction counts
        if job.instructions > 10_000_000_000:  # 10B
            issues.append(f"  Line {job.line_number}: Very large instruction count ({job.instructions:,})")
        
        # Check for missing essential fields
        if not job.trace_file and not job.config_file:
            issues.append(f"  Line {job.line_number}: Missing both trace and config")
        
        # Check for duplicate output directories
        output_dirs = [j.output_dir for j in jobs if j.output_dir]
        if job.output_dir and output_dirs.count(job.output_dir) > 1:
            issues.append(f"  Line {job.line_number}: Duplicate output dir: {job.output_dir}")
    
    # Remove duplicates from issues
    issues = list(set(issues))
    
    if issues:
        return ValidationResult(
            False,
            f"Job sanity issues detected ({len(issues)})",
            severity="warning",
            details='\n'.join(issues[:10])
        )
    
    return ValidationResult(True, f"All {len(jobs)} jobs look sane")


def run_all_checks(jobfile: str, config: Config, verbose: bool = False) -> Tuple[bool, List[ValidationResult]]:
    """Run all validation checks."""
    results = []
    all_passed = True
    has_errors = False
    
    print_header("Parsing Jobfile")
    jobs, parse_errors = parse_jobfile(jobfile, config)
    
    if parse_errors:
        for err in parse_errors:
            print(f"  {Colors.RED}✗{Colors.RESET} {err}")
        has_errors = True
    
    print(f"  {Colors.GREEN}✓{Colors.RESET} Found {len(jobs)} job(s)")
    
    if not jobs:
        return False, [ValidationResult(False, "No jobs found in jobfile")]
    
    # Show job summary
    if verbose:
        print(f"\n  Jobs:")
        for job in jobs[:5]:
            print(f"    - {job.name}: {job.trace_file}")
        if len(jobs) > 5:
            print(f"    ... and {len(jobs) - 5} more")
    
    # Run checks
    print_header("Environment Checks")
    
    checks = [
        ("Sniper Compilation", lambda: check_sniper_compilation(config, verbose)),
        ("Debug Flags", lambda: check_debug_flags(config, verbose)),
    ]
    
    for name, check_func in checks:
        result = check_func()
        results.append(result)
        print_check(name, result)
        if not result.passed and result.severity == "error":
            has_errors = True
    
    print_header("Job Validation")
    
    job_checks = [
        ("Trace Files", lambda: check_trace_files(jobs, verbose)),
        ("Config Files", lambda: check_config_files(jobs, config, verbose)),
        ("Output Directories", lambda: check_output_directories(jobs, verbose)),
        ("Job Sanity", lambda: check_job_sanity(jobs, verbose)),
    ]
    
    for name, check_func in job_checks:
        result = check_func()
        results.append(result)
        print_check(name, result)
        if not result.passed and result.severity == "error":
            has_errors = True
    
    print_header("System Checks")
    
    system_checks = [
        ("Disk Space", lambda: check_disk_space(jobs, verbose)),
        ("SLURM Queue", lambda: check_slurm_queue(jobs, verbose)),
    ]
    
    for name, check_func in system_checks:
        result = check_func()
        results.append(result)
        print_check(name, result)
        if not result.passed and result.severity == "error":
            has_errors = True
    
    return not has_errors, results


def submit_jobs(jobfile: str, dry_run: bool = False) -> bool:
    """Submit jobs from the jobfile."""
    if dry_run:
        print(f"\n{Colors.YELLOW}DRY RUN - Jobs would be submitted from: {jobfile}{Colors.RESET}")
        return True
    
    print(f"\n{Colors.CYAN}Submitting jobs from: {jobfile}{Colors.RESET}")
    
    try:
        with open(jobfile, 'r') as f:
            lines = f.readlines()
        
        submitted = 0
        failed = 0
        
        for line_num, line in enumerate(lines, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            if 'sbatch' not in line and 'native_wrapper' not in line:
                continue
            
            print(f"  Submitting line {line_num}...", end=" ")
            
            try:
                result = subprocess.run(
                    line,
                    shell=True,
                    capture_output=True,
                    text=True,
                    timeout=30
                )
                
                if result.returncode == 0:
                    print(f"{Colors.GREEN}OK{Colors.RESET}")
                    if result.stdout.strip():
                        print(f"    {result.stdout.strip()}")
                    submitted += 1
                else:
                    print(f"{Colors.RED}FAILED{Colors.RESET}")
                    if result.stderr.strip():
                        print(f"    {result.stderr.strip()}")
                    failed += 1
                    
            except subprocess.TimeoutExpired:
                print(f"{Colors.RED}TIMEOUT{Colors.RESET}")
                failed += 1
            except Exception as e:
                print(f"{Colors.RED}ERROR: {e}{Colors.RESET}")
                failed += 1
        
        print(f"\n{Colors.BOLD}Summary: {submitted} submitted, {failed} failed{Colors.RESET}")
        return failed == 0
        
    except Exception as e:
        print(f"{Colors.RED}Error reading jobfile: {e}{Colors.RESET}")
        return False


def get_slurm_queue_size(user: Optional[str] = None) -> int:
    """Return current SLURM queue size for user; 0 on error."""
    try:
        usr = user or os.environ.get('USER', '')
        result = subprocess.run(
            ['squeue', '-u', usr, '-h'],
            capture_output=True,
            text=True,
            timeout=10
        )
        if result.returncode != 0:
            return 0
        lines = [l for l in result.stdout.splitlines() if l.strip()]
        return len(lines)
    except Exception:
        return 0


def submit_jobs_throttled(jobfile: str, max_slurm_jobs: int, retry_delay: float, submit_delay: float, dry_run: bool = False) -> bool:
    """Submit jobs, throttling based on SLURM queue size."""
    if dry_run:
        print(f"\n{Colors.YELLOW}DRY RUN - Jobs would be submitted from: {jobfile}{Colors.RESET}")
        return True

    print(f"\n{Colors.CYAN}Submitting jobs from: {jobfile}{Colors.RESET}")

    try:
        with open(jobfile, 'r') as f:
            lines = f.readlines()

        submitted = 0
        failed = 0

        for line_num, line in enumerate(lines, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if 'sbatch' not in line and 'native_wrapper' not in line and 'run-sniper' not in line:
                continue

            # Throttle until under max queue size
            while max_slurm_jobs and get_slurm_queue_size() >= max_slurm_jobs:
                print(f"  {Colors.YELLOW}Queue limit {max_slurm_jobs} reached. Retrying in {retry_delay}s{Colors.RESET}")
                time.sleep(retry_delay)

            print(f"  Submitting line {line_num}...", end=" ")
            try:
                result = subprocess.run(
                    line,
                    shell=True,
                    capture_output=True,
                    text=True,
                    timeout=30
                )

                if result.returncode == 0:
                    print(f"{Colors.GREEN}OK{Colors.RESET}")
                    if result.stdout.strip():
                        print(f"    {result.stdout.strip()}")
                    submitted += 1
                else:
                    print(f"{Colors.RED}FAILED{Colors.RESET}")
                    if result.stderr.strip():
                        print(f"    {result.stderr.strip()}")
                    failed += 1

            except subprocess.TimeoutExpired:
                print(f"{Colors.RED}TIMEOUT{Colors.RESET}")
                failed += 1
            except Exception as e:
                print(f"{Colors.RED}ERROR: {e}{Colors.RESET}")
                failed += 1

            # Small delay between submissions
            time.sleep(submit_delay)

        print(f"\n{Colors.BOLD}Summary: {submitted} submitted, {failed} failed{Colors.RESET}")
        return failed == 0

    except Exception as e:
        print(f"{Colors.RED}Error reading jobfile: {e}{Colors.RESET}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Safe job submission with comprehensive validation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    %(prog)s experiments/exp_suite_name/jobfile.sh
    %(prog)s experiments/exp_suite_name/jobfile.sh --dry-run
    %(prog)s experiments/exp_suite_name/jobfile.sh --force --verbose
        """
    )
    
    parser.add_argument(
        'jobfile',
        help="Path to the jobfile.sh containing sbatch commands"
    )
    parser.add_argument(
        '--dry-run', '-n',
        action='store_true',
        help="Validate but don't submit jobs"
    )
    parser.add_argument(
        '--force', '-f',
        action='store_true',
        help="Submit even if some checks fail (warnings only)"
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help="Show detailed output"
    )
    parser.add_argument(
        '--no-color',
        action='store_true',
        help="Disable colored output"
    )
    parser.add_argument(
        '--sniper-dir',
        help="Override sniper directory path"
    )
    parser.add_argument(
        '--max-slurm-jobs',
        type=int,
        default=1000,
        help="Maximum number of SLURM jobs allowed in queue; waits if exceeded"
    )
    parser.add_argument(
        '--slurm-retry-delay',
        type=float,
        default=SLURM_RETRY_DELAY,
        help="Seconds to wait before re-checking SLURM queue when at limit"
    )
    parser.add_argument(
        '--slurm-submit-delay',
        type=float,
        default=SLURM_SUBMIT_DELAY,
        help="Seconds to sleep between job submissions"
    )
    # Note: exp-dir support removed; provide jobfile explicitly
    
    args = parser.parse_args()
    
    if args.no_color:
        Colors.disable()
    
    # Initialize configuration
    config = Config()
    if args.sniper_dir:
        config.sniper_dir = args.sniper_dir
        config.debug_config = os.path.join(args.sniper_dir, "config", "debug.cfg")
    
    print(f"{Colors.BOLD}{Colors.MAGENTA}")
    print("╔════════════════════════════════════════════════════════════╗")
    print("║              SAFE SUBMIT - Job Validation                  ║")
    print("╚════════════════════════════════════════════════════════════╝")
    print(f"{Colors.RESET}")
    
    jobfile = args.jobfile
    print(f"Jobfile: {Colors.CYAN}{jobfile}{Colors.RESET}")
    print(f"Sniper:  {Colors.CYAN}{config.sniper_dir or 'Not found'}{Colors.RESET}")
    
    # Run validation
    passed, results = run_all_checks(jobfile, config, args.verbose)
    
    # Summary
    print_header("Summary")
    
    errors = sum(1 for r in results if not r.passed and r.severity == "error")
    warnings = sum(1 for r in results if not r.passed and r.severity == "warning")
    passed_count = sum(1 for r in results if r.passed)
    
    print(f"  {Colors.GREEN}Passed:   {passed_count}{Colors.RESET}")
    print(f"  {Colors.YELLOW}Warnings: {warnings}{Colors.RESET}")
    print(f"  {Colors.RED}Errors:   {errors}{Colors.RESET}")
    
    # Decision
    if errors > 0 and not args.force:
        print(f"\n{Colors.RED}{Colors.BOLD}✗ Validation FAILED - Jobs not submitted{Colors.RESET}")
        print(f"  Use --force to submit anyway (not recommended)")
        sys.exit(1)
    elif errors > 0 and args.force:
        print(f"\n{Colors.YELLOW}{Colors.BOLD}⚠ Forcing submission despite errors{Colors.RESET}")
    elif warnings > 0:
        print(f"\n{Colors.YELLOW}{Colors.BOLD}⚠ Validation passed with warnings{Colors.RESET}")
    else:
        print(f"\n{Colors.GREEN}{Colors.BOLD}✓ All checks passed!{Colors.RESET}")
    
    # Submit or dry-run
    if args.dry_run:
        print(f"\n{Colors.CYAN}Dry run mode - no jobs submitted{Colors.RESET}")
        sys.exit(0)
    
    # Ask for confirmation if there were warnings
    if warnings > 0 and not args.force:
        try:
            response = input(f"\n{Colors.YELLOW}Submit jobs despite warnings? [y/N]: {Colors.RESET}")
            if response.lower() != 'y':
                print("Aborted.")
                sys.exit(0)
        except (EOFError, KeyboardInterrupt):
            print("\nAborted.")
            sys.exit(0)
    
    # Submit (throttled)
    success = submit_jobs_throttled(
        jobfile=jobfile,
        max_slurm_jobs=args.max_slurm_jobs,
        retry_delay=args.slurm_retry_delay,
        submit_delay=args.slurm_submit_delay,
        dry_run=False
    )
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
