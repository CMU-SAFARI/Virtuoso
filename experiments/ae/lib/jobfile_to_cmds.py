#!/usr/bin/env python3
"""Extract the bare run-sniper commands from a generated jobfile.sh.

Each jobfile line looks like:
  sbatch <opts> <native_wrapper.sh> " <run-sniper ...> "
native_wrapper.sh is just `sh -c "$1"`, so the quoted final argument IS the
command to run. We strip the sbatch wrapper and emit that command, so the same
jobs can run locally (no SLURM). Commands are emitted NUL-separated for safe
piping into `xargs -0`.

Usage: jobfile_to_cmds.py JOBFILE   # writes NUL-separated commands to stdout
"""
import sys, re

def extract(line):
    line=line.strip()
    if not line.startswith("sbatch"):
        return None
    # The command is the last double-quoted string on the line
    # (the argument passed to native_wrapper.sh). Grab the substring between
    # the first '"' that follows native_wrapper.sh and the final '"'.
    m=re.search(r'native_wrapper\.sh\s+"(.*)"\s*$', line)
    if m:
        return m.group(1).strip()
    # Fallback: last quoted segment
    q=re.findall(r'"([^"]*)"', line)
    return q[-1].strip() if q else None

def main():
    if len(sys.argv)!=2:
        sys.exit("usage: jobfile_to_cmds.py JOBFILE")
    n=0
    with open(sys.argv[1]) as f:
        for line in f:
            cmd=extract(line)
            if cmd:
                sys.stdout.write(cmd+"\0")
                n+=1
    sys.stderr.write(f"[jobfile_to_cmds] {n} commands\n")

if __name__=="__main__":
    main()
