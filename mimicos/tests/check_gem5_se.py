#!/usr/bin/env python3
"""End-to-end check of the MimicOS hook in gem5's syscall-emulation mode.

Runs the same workload twice -- once with stock SE-mode allocation, once with
MimicOS -- and asserts the properties the integration is supposed to provide,
plus the ones it must not break.

What it checks, and why each one is here:

  program output identical
      MimicOS changes *where* pages live, never what the program computes. A
      difference means the physical mapping is wrong, not merely different.

  MimicOS costs more ticks
      SE mode resolves minor faults in zero simulated time. If the tick counts
      match, the calibrated fault cost is not reaching the thread and the
      quiesce hook in PageFault::invoke is dead.

  physical space is disjoint
      MimicOS's frames are carved out of the MemPool up front. If the reserved
      region is too small, its top-end data frames alias whatever gem5 later
      hands out for its own structures -- a bug that produces plausible-looking
      results rather than a crash, so it is checked directly.

  mapping equivalence
      The trace gem5 produced must replay identically against libmimicos on its
      own, proving the adapter is a pass-through. See check_equivalence.py.

Usage:
    check_gem5_se.py --gem5 simulator/gem5/build/X86/gem5.opt \
                     --config mimicos/configs/embedded_4gb_thp.ini
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MIMICOS_ROOT = os.path.dirname(HERE)


def run_gem5(gem5, script, outdir, mimicos_config=None, mapping_trace=None):
    """Run gem5 once, returning (stdout+stderr, ticks)."""
    cmd = [gem5, f"--outdir={outdir}", script, "--cpu-type", "timing"]
    if mimicos_config:
        cmd += ["--mimicos-config", mimicos_config]

    env = dict(os.environ)
    if mapping_trace:
        env["MIMICOS_MAPPING_TRACE"] = mapping_trace
    else:
        env.pop("MIMICOS_MAPPING_TRACE", None)

    result = subprocess.run(cmd, capture_output=True, text=True, env=env)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        print(output)
        sys.exit(f"gem5 exited with code {result.returncode}")

    match = re.search(r"Exiting @ tick (\d+)", output)
    if not match:
        print(output)
        sys.exit("could not find the exit tick in gem5's output")
    return output, int(match.group(1))


def program_output(text):
    """The workload's own stdout, stripped of gem5's chatter."""
    skip = ("gem5 ", "info:", "warn:", "command line:", "Beginning simulation",
            "MimicOS", "Exiting @", "src/", "[Buddy]", "Global frequency")
    return [line for line in text.splitlines()
            if line.strip() and not line.startswith(skip)]


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"  [{status}] {name}" + (f" -- {detail}" if detail else ""))
    return 0 if condition else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--gem5", required=True, help="path to gem5.opt")
    parser.add_argument("--config", required=True, help="MimicOS INI")
    parser.add_argument("--script", help="gem5 config script "
                        "(default: configs/example/mimicos_se.py next to gem5)")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    gem5 = os.path.abspath(args.gem5)
    if not os.path.exists(gem5):
        sys.exit(f"gem5 binary not found: {gem5}")

    script = args.script
    if not script:
        # build/X86/gem5.opt -> ../../../configs/example/mimicos_se.py
        gem5_root = os.path.dirname(os.path.dirname(os.path.dirname(gem5)))
        script = os.path.join(gem5_root, "configs", "example", "mimicos_se.py")
    if not os.path.exists(script):
        sys.exit(f"gem5 config script not found: {script}")

    config = os.path.abspath(args.config)
    if not os.path.exists(config):
        sys.exit(f"MimicOS config not found: {config}")

    workdir = tempfile.mkdtemp(prefix="mimicos_gem5_")
    trace = os.path.join(workdir, "gem5_mapping.csv")
    failures = 0

    print("Running gem5 with stock SE-mode allocation")
    stock_out, stock_ticks = run_gem5(
        gem5, script, os.path.join(workdir, "m5out_stock"))
    print(f"  {stock_ticks} ticks")

    print("Running gem5 with MimicOS")
    mimicos_out, mimicos_ticks = run_gem5(
        gem5, script, os.path.join(workdir, "m5out_mimicos"),
        mimicos_config=config, mapping_trace=trace)
    print(f"  {mimicos_ticks} ticks")

    print("\nChecks:")

    failures += check(
        "program output is identical",
        program_output(stock_out) == program_output(mimicos_out),
        f"{program_output(stock_out)} vs {program_output(mimicos_out)}")

    failures += check(
        "MimicOS was actually attached",
        "MimicOS attached from" in mimicos_out)

    failures += check(
        "MimicOS charges a fault cost",
        mimicos_ticks > stock_ticks,
        f"{mimicos_ticks} vs {stock_ticks} ticks "
        f"({100.0 * (mimicos_ticks - stock_ticks) / stock_ticks:+.1f}%)")

    # The reservation must span MimicOS's whole physical space, kernel reserve
    # included, or its top-end frames overlap gem5's own allocations.
    reserved = re.search(r"MimicOS reserved (\d+) MiB", mimicos_out)
    config_mb = None
    with open(config) as handle:
        for line in handle:
            if line.strip().startswith("memory_size"):
                config_mb = int(line.split("=")[1].strip())
                break
    failures += check(
        "reservation covers the whole MimicOS physical space",
        reserved is not None and config_mb is not None
        and int(reserved.group(1)) == config_mb,
        f"reserved {reserved.group(1) if reserved else '?'} MiB, "
        f"config declares {config_mb} MiB")

    if os.path.exists(trace) and os.path.getsize(trace) > 0:
        print("\nMapping equivalence:")
        result = subprocess.run(
            [sys.executable, os.path.join(HERE, "check_equivalence.py"),
             "--config", config, "--trace", trace],
            capture_output=True, text=True)
        for line in result.stdout.splitlines():
            print(f"  {line}")
        failures += check("gem5 and the reference agree",
                          result.returncode == 0)
    else:
        failures += check("gem5 emitted a mapping trace", False,
                          f"{trace} is missing or empty")

    if args.keep:
        print(f"\nArtifacts kept in {workdir}")

    print()
    if failures:
        print(f"{failures} check(s) FAILED")
        return 1
    print("All checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
