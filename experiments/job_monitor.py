#!/usr/bin/env python3
"""
SLURM Job Monitor - A beautiful terminal-based job monitoring tool

Features:
- Real-time job status monitoring
- Job history and statistics
- Resource usage visualization
- Auto-refresh with configurable interval
- Color-coded status indicators
- Job filtering and sorting

Usage:
    python job_monitor.py              # Monitor your jobs
    python job_monitor.py -u all       # Monitor all users
    python job_monitor.py -w           # Watch mode (auto-refresh)
    python job_monitor.py --history    # Show completed jobs
"""

import argparse
import subprocess
import sys
import os
import time
from datetime import datetime, timedelta
from dataclasses import dataclass
from typing import List, Dict, Optional, Tuple
import re


# ============================================================================
# ANSI Colors and Styling
# ============================================================================

class Style:
    """ANSI escape codes for terminal styling."""
    # Colors
    BLACK = '\033[30m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    GRAY = '\033[90m'
    
    # Backgrounds
    BG_RED = '\033[41m'
    BG_GREEN = '\033[42m'
    BG_YELLOW = '\033[43m'
    BG_BLUE = '\033[44m'
    
    # Styles
    BOLD = '\033[1m'
    DIM = '\033[2m'
    ITALIC = '\033[3m'
    UNDERLINE = '\033[4m'
    BLINK = '\033[5m'
    REVERSE = '\033[7m'
    
    # Reset
    RESET = '\033[0m'
    
    # Special
    CLEAR_SCREEN = '\033[2J\033[H'
    CLEAR_LINE = '\033[2K'
    
    @classmethod
    def disable(cls):
        """Disable all colors."""
        for attr in dir(cls):
            if not attr.startswith('_') and isinstance(getattr(cls, attr), str):
                if attr not in ('disable', 'CLEAR_SCREEN', 'CLEAR_LINE'):
                    setattr(cls, attr, '')


# ============================================================================
# Box Drawing Characters
# ============================================================================

class Box:
    """Unicode box drawing characters."""
    # Corners
    TL = '╭'  # Top Left
    TR = '╮'  # Top Right
    BL = '╰'  # Bottom Left
    BR = '╯'  # Bottom Right
    
    # Lines
    H = '─'   # Horizontal
    V = '│'   # Vertical
    
    # T-junctions
    TH = '┬'  # T pointing down
    BH = '┴'  # T pointing up
    LV = '├'  # T pointing right
    RV = '┤'  # T pointing left
    
    # Cross
    X = '┼'
    
    # Double lines
    DH = '═'
    DV = '║'
    DTL = '╔'
    DTR = '╗'
    DBL = '╚'
    DBR = '╝'


# ============================================================================
# Data Structures
# ============================================================================

@dataclass
class Job:
    """Represents a SLURM job."""
    job_id: str
    name: str
    user: str
    partition: str
    state: str
    time_used: str
    time_limit: str
    nodes: int
    cpus: int
    node_list: str
    submit_time: str = ""
    start_time: str = ""
    reason: str = ""
    
    @property
    def state_color(self) -> str:
        """Get color based on job state."""
        state_colors = {
            'RUNNING': Style.GREEN,
            'PENDING': Style.YELLOW,
            'COMPLETING': Style.CYAN,
            'COMPLETED': Style.BLUE,
            'FAILED': Style.RED,
            'CANCELLED': Style.MAGENTA,
            'TIMEOUT': Style.RED,
            'NODE_FAIL': Style.RED,
            'PREEMPTED': Style.YELLOW,
            'SUSPENDED': Style.GRAY,
        }
        return state_colors.get(self.state, Style.WHITE)
    
    @property
    def state_icon(self) -> str:
        """Get icon based on job state."""
        state_icons = {
            'RUNNING': '▶',
            'PENDING': '⏳',
            'COMPLETING': '⏹',
            'COMPLETED': '✓',
            'FAILED': '✗',
            'CANCELLED': '⊘',
            'TIMEOUT': '⏱',
            'NODE_FAIL': '⚠',
            'PREEMPTED': '⏸',
            'SUSPENDED': '⏸',
        }
        return state_icons.get(self.state, '?')


@dataclass
class ClusterStats:
    """Cluster-wide statistics."""
    total_nodes: int = 0
    nodes_up: int = 0
    nodes_down: int = 0
    nodes_allocated: int = 0
    nodes_idle: int = 0
    total_cpus: int = 0
    cpus_allocated: int = 0
    jobs_running: int = 0
    jobs_pending: int = 0


# ============================================================================
# SLURM Interface
# ============================================================================

def run_command(cmd: List[str], timeout: int = 30) -> Tuple[bool, str]:
    """Run a shell command and return output."""
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        return result.returncode == 0, result.stdout
    except subprocess.TimeoutExpired:
        return False, "Command timed out"
    except FileNotFoundError:
        return False, "Command not found"
    except Exception as e:
        return False, str(e)


def get_jobs(user: str = None, all_users: bool = False) -> List[Job]:
    """Get list of jobs from SLURM."""
    cmd = [
        'squeue',
        '-h',
        '-o', '%i|%j|%u|%P|%T|%M|%l|%D|%C|%R|%V|%S'
    ]
    
    if not all_users:
        if user:
            cmd.extend(['-u', user])
        else:
            cmd.extend(['-u', os.environ.get('USER', '')])
    
    success, output = run_command(cmd)
    
    if not success:
        return []
    
    jobs = []
    for line in output.strip().split('\n'):
        if not line:
            continue
        
        parts = line.split('|')
        if len(parts) >= 10:
            job = Job(
                job_id=parts[0],
                name=parts[1],  # Keep full name
                user=parts[2],
                partition=parts[3],
                state=parts[4],
                time_used=parts[5],
                time_limit=parts[6],
                nodes=int(parts[7]) if parts[7].isdigit() else 0,
                cpus=int(parts[8]) if parts[8].isdigit() else 0,
                node_list=parts[9] if not parts[9].startswith('(') else '',
                reason=parts[9] if parts[9].startswith('(') else '',
                submit_time=parts[10] if len(parts) > 10 else '',
                start_time=parts[11] if len(parts) > 11 else '',
            )
            jobs.append(job)
    
    return jobs


def get_job_history(user: str = None, days: int = 1) -> List[Job]:
    """Get completed jobs from sacct."""
    cmd = [
        'sacct',
        '-h',
        '-X',  # No job steps
        '-o', 'JobID,JobName%30,User,Partition,State,Elapsed,Timelimit,NNodes,NCPUs,NodeList',
        '--starttime', f'now-{days}days',
        '-P',  # Parseable
    ]
    
    if user:
        cmd.extend(['-u', user])
    else:
        cmd.extend(['-u', os.environ.get('USER', '')])
    
    success, output = run_command(cmd)
    
    if not success:
        return []
    
    jobs = []
    for line in output.strip().split('\n'):
        if not line:
            continue
        
        parts = line.split('|')
        if len(parts) >= 10:
            job = Job(
                job_id=parts[0],
                name=parts[1][:30],
                user=parts[2],
                partition=parts[3],
                state=parts[4].split()[0],  # Remove exit code
                time_used=parts[5],
                time_limit=parts[6],
                nodes=int(parts[7]) if parts[7].isdigit() else 0,
                cpus=int(parts[8]) if parts[8].isdigit() else 0,
                node_list=parts[9],
            )
            jobs.append(job)
    
    return jobs


def get_cluster_stats() -> ClusterStats:
    """Get cluster-wide statistics."""
    stats = ClusterStats()
    
    # Get node info
    success, output = run_command(['sinfo', '-h', '-o', '%D %t %c'])
    if success:
        for line in output.strip().split('\n'):
            parts = line.split()
            if len(parts) >= 2:
                count = int(parts[0])
                state = parts[1]
                
                stats.total_nodes += count
                if 'alloc' in state:
                    stats.nodes_allocated += count
                elif 'idle' in state:
                    stats.nodes_idle += count
                elif 'down' in state or 'drain' in state:
                    stats.nodes_down += count
                else:
                    stats.nodes_up += count
    
    # Get job counts
    success, output = run_command(['squeue', '-h', '-o', '%T'])
    if success:
        for line in output.strip().split('\n'):
            if line == 'RUNNING':
                stats.jobs_running += 1
            elif line == 'PENDING':
                stats.jobs_pending += 1
    
    return stats


# ============================================================================
# Display Functions
# ============================================================================

def get_terminal_width() -> int:
    """Get terminal width."""
    try:
        return os.get_terminal_size().columns
    except:
        return 120


def draw_box(title: str, content: List[str], width: int = None) -> str:
    """Draw a box around content."""
    if width is None:
        width = get_terminal_width() - 2
    
    inner_width = width - 2
    
    lines = []
    
    # Top border with title
    title_str = f" {title} " if title else ""
    padding = inner_width - len(title_str)
    left_pad = padding // 2
    right_pad = padding - left_pad
    
    lines.append(f"{Style.CYAN}{Box.TL}{Box.H * left_pad}{Style.BOLD}{title_str}{Style.RESET}{Style.CYAN}{Box.H * right_pad}{Box.TR}{Style.RESET}")
    
    # Content
    for line in content:
        # Strip ANSI codes for length calculation
        clean_line = re.sub(r'\033\[[0-9;]*m', '', line)
        padding = inner_width - len(clean_line)
        lines.append(f"{Style.CYAN}{Box.V}{Style.RESET}{line}{' ' * padding}{Style.CYAN}{Box.V}{Style.RESET}")
    
    # Bottom border
    lines.append(f"{Style.CYAN}{Box.BL}{Box.H * inner_width}{Box.BR}{Style.RESET}")
    
    return '\n'.join(lines)


def format_time_bar(used: str, limit: str, width: int = 20) -> str:
    """Create a visual time progress bar."""
    def parse_time(t: str) -> int:
        """Parse time string to seconds."""
        if not t or t == 'UNLIMITED':
            return 0
        
        parts = t.replace('-', ':').split(':')
        parts = [int(p) for p in parts if p.isdigit()]
        
        if len(parts) == 3:
            return parts[0] * 3600 + parts[1] * 60 + parts[2]
        elif len(parts) == 4:
            return parts[0] * 86400 + parts[1] * 3600 + parts[2] * 60 + parts[3]
        return 0
    
    used_sec = parse_time(used)
    limit_sec = parse_time(limit)
    
    if limit_sec == 0:
        return f"{Style.GRAY}{'░' * width}{Style.RESET}"
    
    ratio = min(used_sec / limit_sec, 1.0)
    filled = int(ratio * width)
    
    if ratio < 0.5:
        color = Style.GREEN
    elif ratio < 0.8:
        color = Style.YELLOW
    else:
        color = Style.RED
    
    bar = f"{color}{'█' * filled}{Style.GRAY}{'░' * (width - filled)}{Style.RESET}"
    return bar


def display_header():
    """Display the application header."""
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    header = f"""
{Style.BOLD}{Style.CYAN}╔══════════════════════════════════════════════════════════════════════════════╗
║                         {Style.WHITE}🖥  SLURM Job Monitor  🖥{Style.CYAN}                           ║
╚══════════════════════════════════════════════════════════════════════════════╝{Style.RESET}
                                                              {Style.DIM}{now}{Style.RESET}
"""
    print(header)


def display_cluster_summary(stats: ClusterStats):
    """Display cluster summary."""
    content = [
        f"  {Style.BOLD}Nodes:{Style.RESET}  {Style.GREEN}●{Style.RESET} Allocated: {stats.nodes_allocated}  "
        f"{Style.CYAN}●{Style.RESET} Idle: {stats.nodes_idle}  "
        f"{Style.RED}●{Style.RESET} Down: {stats.nodes_down}  "
        f"{Style.DIM}Total: {stats.total_nodes}{Style.RESET}",
        f"  {Style.BOLD}Jobs:{Style.RESET}   {Style.GREEN}▶{Style.RESET} Running: {stats.jobs_running}  "
        f"{Style.YELLOW}⏳{Style.RESET} Pending: {stats.jobs_pending}",
    ]
    
    print(draw_box("Cluster Status", content))


def display_jobs(jobs: List[Job], title: str = "Your Jobs"):
    """Display jobs in a formatted table."""
    if not jobs:
        content = [f"  {Style.DIM}No jobs found{Style.RESET}"]
        print(draw_box(title, content))
        return
    
    width = get_terminal_width() - 4
    
    # Calculate dynamic name width based on terminal size
    # Minimum columns needed: ID(10) + User(10) + State(12) + Time(27) + Nodes(5) + CPUs(5) + Node(12) + spacing(10) = 91
    name_width = max(35, min(70, width - 95))
    
    # Header
    header = (
        f"  {Style.BOLD}{Style.WHITE}"
        f"{'ID':<10} {'User':<10} {'Name':<{name_width}} {'State':<12} {'Time':^27} {'Nodes':>5} {'CPUs':>5} {'Node/Reason':<12}"
        f"{Style.RESET}"
    )
    
    separator = f"  {Style.DIM}{'─' * (width - 4)}{Style.RESET}"
    
    content = [header, separator]
    
    # Sort jobs: running first, then pending, then others
    state_order = {'RUNNING': 0, 'COMPLETING': 1, 'PENDING': 2}
    jobs_sorted = sorted(jobs, key=lambda j: (state_order.get(j.state, 9), j.job_id))
    
    for job in jobs_sorted:
        time_bar = format_time_bar(job.time_used, job.time_limit, 15)
        
        node_info = job.node_list or job.reason
        if len(node_info) > 12:
            node_info = node_info[:9] + '...'
        
        # Truncate name if needed for display
        display_name = job.name if len(job.name) <= name_width else job.name[:name_width-3] + '...'
        
        # Truncate user if needed
        display_user = job.user if len(job.user) <= 10 else job.user[:7] + '...'
        
        line = (
            f"  {Style.WHITE}{job.job_id:<10}{Style.RESET} "
            f"{Style.MAGENTA}{display_user:<10}{Style.RESET} "
            f"{display_name:<{name_width}} "
            f"{job.state_color}{job.state_icon} {job.state:<10}{Style.RESET} "
            f"{job.time_used:>12} {time_bar} "
            f"{job.nodes:>5} "
            f"{job.cpus:>5} "
            f"{Style.DIM}{node_info:<12}{Style.RESET}"
        )
        content.append(line)
    
    # Summary
    running = sum(1 for j in jobs if j.state == 'RUNNING')
    pending = sum(1 for j in jobs if j.state == 'PENDING')
    total_cpus = sum(j.cpus for j in jobs if j.state == 'RUNNING')
    
    content.append(separator)
    content.append(
        f"  {Style.BOLD}Total:{Style.RESET} {len(jobs)} jobs  "
        f"{Style.GREEN}▶{Style.RESET} {running} running  "
        f"{Style.YELLOW}⏳{Style.RESET} {pending} pending  "
        f"{Style.CYAN}CPUs:{Style.RESET} {total_cpus}"
    )
    
    print(draw_box(title, content))


def display_job_history(jobs: List[Job]):
    """Display job history."""
    if not jobs:
        content = [f"  {Style.DIM}No recent jobs found{Style.RESET}"]
        print(draw_box("Job History (Last 24h)", content))
        return
    
    width = get_terminal_width() - 4
    
    # Calculate dynamic name width
    name_width = max(30, min(60, width - 60))
    
    header = (
        f"  {Style.BOLD}{Style.WHITE}"
        f"{'ID':<12} {'Name':<{name_width}} {'State':<12} {'Elapsed':>12} {'Nodes':>6} {'CPUs':>6}"
        f"{Style.RESET}"
    )
    
    separator = f"  {Style.DIM}{'─' * (width - 4)}{Style.RESET}"
    
    content = [header, separator]
    
    for job in jobs[:20]:  # Limit to 20 most recent
        display_name = job.name if len(job.name) <= name_width else job.name[:name_width-3] + '...'
        line = (
            f"  {Style.WHITE}{job.job_id:<12}{Style.RESET} "
            f"{display_name:<{name_width}} "
            f"{job.state_color}{job.state_icon} {job.state:<10}{Style.RESET} "
            f"{job.time_used:>12} "
            f"{job.nodes:>6} "
            f"{job.cpus:>6}"
        )
        content.append(line)
    
    # Stats
    completed = sum(1 for j in jobs if j.state == 'COMPLETED')
    failed = sum(1 for j in jobs if j.state in ('FAILED', 'TIMEOUT', 'CANCELLED'))
    
    content.append(separator)
    content.append(
        f"  {Style.BOLD}Summary:{Style.RESET} {len(jobs)} jobs  "
        f"{Style.GREEN}✓{Style.RESET} {completed} completed  "
        f"{Style.RED}✗{Style.RESET} {failed} failed/cancelled"
    )
    
    print(draw_box("Job History (Last 24h)", content))


def display_help():
    """Display keyboard shortcuts."""
    content = [
        f"  {Style.BOLD}q{Style.RESET} Quit    "
        f"{Style.BOLD}r{Style.RESET} Refresh    "
        f"{Style.BOLD}h{Style.RESET} History    "
        f"{Style.BOLD}a{Style.RESET} All users    "
        f"{Style.BOLD}c{Style.RESET} Cancel job"
    ]
    print(draw_box("Keyboard Shortcuts", content))


def cancel_job(job_id: str) -> Tuple[bool, str]:
    """Cancel a SLURM job."""
    success, output = run_command(['scancel', job_id])
    if success:
        return True, f"Job {job_id} cancelled successfully"
    else:
        return False, f"Failed to cancel job {job_id}: {output}"


def get_job_details(job_id: str) -> Optional[Dict]:
    """Get detailed information about a specific job."""
    cmd = [
        'scontrol', 'show', 'job', job_id
    ]
    
    success, output = run_command(cmd)
    if not success:
        return None
    
    details = {}
    for line in output.split():
        if '=' in line:
            key, _, value = line.partition('=')
            details[key] = value
    
    return details


def display_job_details(job_id: str):
    """Display detailed job information."""
    details = get_job_details(job_id)
    
    if not details:
        print(f"{Style.RED}Job {job_id} not found{Style.RESET}")
        return
    
    content = [
        f"  {Style.BOLD}Job ID:{Style.RESET}      {details.get('JobId', 'N/A')}",
        f"  {Style.BOLD}Name:{Style.RESET}        {details.get('JobName', 'N/A')}",
        f"  {Style.BOLD}User:{Style.RESET}        {details.get('UserId', 'N/A')}",
        f"  {Style.BOLD}State:{Style.RESET}       {details.get('JobState', 'N/A')}",
        f"  {Style.BOLD}Partition:{Style.RESET}   {details.get('Partition', 'N/A')}",
        f"  {Style.BOLD}Nodes:{Style.RESET}       {details.get('NodeList', 'N/A')}",
        f"  {Style.BOLD}CPUs:{Style.RESET}        {details.get('NumCPUs', 'N/A')}",
        f"  {Style.BOLD}Memory:{Style.RESET}      {details.get('MinMemoryNode', 'N/A')}",
        "",
        f"  {Style.BOLD}Submit Time:{Style.RESET} {details.get('SubmitTime', 'N/A')}",
        f"  {Style.BOLD}Start Time:{Style.RESET}  {details.get('StartTime', 'N/A')}",
        f"  {Style.BOLD}Time Limit:{Style.RESET}  {details.get('TimeLimit', 'N/A')}",
        f"  {Style.BOLD}Run Time:{Style.RESET}    {details.get('RunTime', 'N/A')}",
        "",
        f"  {Style.BOLD}Work Dir:{Style.RESET}    {details.get('WorkDir', 'N/A')}",
        f"  {Style.BOLD}Command:{Style.RESET}     {details.get('Command', 'N/A')}",
        f"  {Style.BOLD}StdOut:{Style.RESET}      {details.get('StdOut', 'N/A')}",
        f"  {Style.BOLD}StdErr:{Style.RESET}      {details.get('StdErr', 'N/A')}",
    ]
    
    print(draw_box(f"Job Details: {job_id}", content))


def display_node_view(jobs: List[Job]):
    """Display jobs grouped by node."""
    if not jobs:
        content = [f"  {Style.DIM}No jobs found{Style.RESET}"]
        print(draw_box("Node View", content))
        return
    
    # Group jobs by node
    nodes = {}
    for job in jobs:
        if job.node_list and job.state == 'RUNNING':
            node = job.node_list.split(',')[0]  # Get first node
            if node not in nodes:
                nodes[node] = []
            nodes[node].append(job)
    
    content = []
    for node in sorted(nodes.keys()):
        node_jobs = nodes[node]
        total_cpus = sum(j.cpus for j in node_jobs)
        content.append(
            f"  {Style.BOLD}{Style.CYAN}{node}{Style.RESET}: "
            f"{len(node_jobs)} jobs, {total_cpus} CPUs"
        )
        for job in node_jobs[:3]:  # Show first 3 jobs per node
            content.append(
                f"    {Style.DIM}└─{Style.RESET} {job.job_id} {job.user}: {job.name[:30]}"
            )
        if len(node_jobs) > 3:
            content.append(f"    {Style.DIM}   ... and {len(node_jobs) - 3} more{Style.RESET}")
    
    print(draw_box("Node View", content))


# ============================================================================
# Main Application
# ============================================================================

def monitor_once(args):
    """Run a single monitoring cycle."""
    print(Style.CLEAR_SCREEN, end='')
    display_header()
    
    # Cluster stats
    stats = get_cluster_stats()
    display_cluster_summary(stats)
    print()
    
    # Current jobs
    user = None if args.user == 'all' else args.user
    jobs = get_jobs(user=user, all_users=(args.user == 'all'))
    
    # Filter by state
    if args.state == 'running':
        jobs = [j for j in jobs if j.state == 'RUNNING']
    elif args.state == 'pending':
        jobs = [j for j in jobs if j.state == 'PENDING']
    
    # Filter by partition
    if args.partition:
        jobs = [j for j in jobs if j.partition == args.partition]
    
    title = "All Jobs" if args.user == 'all' else f"Jobs for {args.user or os.environ.get('USER', 'you')}"
    if args.state != 'all':
        title += f" ({args.state})"
    if args.partition:
        title += f" on {args.partition}"
    
    display_jobs(jobs, title)
    print()
    
    # Node view if requested
    if args.nodes:
        all_jobs = get_jobs(all_users=True)
        display_node_view(all_jobs)
        print()
    
    # History if requested
    if args.history:
        history = get_job_history(user=user, days=args.days)
        display_job_history(history)
        print()
    
    # Help
    if args.watch:
        print(f"\n  {Style.DIM}Refreshing every {args.interval}s... Press Ctrl+C to stop{Style.RESET}")


def watch_mode(args):
    """Run in watch mode with auto-refresh."""
    try:
        while True:
            monitor_once(args)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print(f"\n{Style.CYAN}Goodbye!{Style.RESET}")
        sys.exit(0)


def main():
    parser = argparse.ArgumentParser(
        description="Beautiful SLURM job monitoring tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    %(prog)s                    # Show your current jobs
    %(prog)s -w                 # Watch mode (auto-refresh)
    %(prog)s -u all             # Show all users' jobs
    %(prog)s --history          # Include job history
    %(prog)s -w -i 5            # Watch with 5s refresh interval
    %(prog)s -k 12345           # Cancel job 12345
    %(prog)s -d 12345           # Show details for job 12345
    %(prog)s --nodes            # Show node-centric view
    %(prog)s -s running         # Show only running jobs
        """
    )
    
    parser.add_argument(
        '-u', '--user',
        default=None,
        help="User to monitor (use 'all' for all users)"
    )
    parser.add_argument(
        '-w', '--watch',
        action='store_true',
        help="Watch mode - auto-refresh"
    )
    parser.add_argument(
        '-i', '--interval',
        type=int,
        default=10,
        help="Refresh interval in seconds (default: 10)"
    )
    parser.add_argument(
        '--history',
        action='store_true',
        help="Show job history"
    )
    parser.add_argument(
        '--days',
        type=int,
        default=1,
        help="Days of history to show (default: 1)"
    )
    parser.add_argument(
        '--no-color',
        action='store_true',
        help="Disable colored output"
    )
    parser.add_argument(
        '-k', '--kill',
        metavar='JOB_ID',
        help="Cancel a job by ID"
    )
    parser.add_argument(
        '-d', '--details',
        metavar='JOB_ID',
        help="Show detailed info for a job"
    )
    parser.add_argument(
        '--nodes',
        action='store_true',
        help="Show node-centric view"
    )
    parser.add_argument(
        '-s', '--state',
        choices=['running', 'pending', 'all'],
        default='all',
        help="Filter by job state"
    )
    parser.add_argument(
        '-p', '--partition',
        help="Filter by partition"
    )
    
    args = parser.parse_args()
    
    if args.no_color:
        Style.disable()
    
    # Handle cancel job
    if args.kill:
        success, message = cancel_job(args.kill)
        if success:
            print(f"{Style.GREEN}✓ {message}{Style.RESET}")
        else:
            print(f"{Style.RED}✗ {message}{Style.RESET}")
        sys.exit(0 if success else 1)
    
    # Handle job details
    if args.details:
        display_header()
        display_job_details(args.details)
        sys.exit(0)
    
    if args.watch:
        watch_mode(args)
    else:
        monitor_once(args)


if __name__ == "__main__":
    main()
