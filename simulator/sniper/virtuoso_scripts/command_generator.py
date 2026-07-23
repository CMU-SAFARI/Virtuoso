#!/usr/bin/env python3
import os
import sys
import subprocess

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <instruction_count>", file=sys.stderr)
        sys.exit(1)

    instructions = sys.argv[1]

    configs = {
        "pomtlb": "./config/virtuoso_configs_v2/pom-tlb.cfg",
        "dmt": "./config/virtuoso_configs_v2/dmt.cfg",
        "asap": "./config/virtuoso_configs_v2/asap.cfg",
        "reservethp": "./config/virtuoso_configs_v2/reservethp.cfg",
        "revelator": "./config/virtuoso_configs_v2/revelator.cfg",
        "revelator_open_addressing": "./config/virtuoso_configs_v2/revelator_open.cfg",
        "revelator_thp": "./config/virtuoso_configs_v2/revelator_thp.cfg",
        "spot": "./config/virtuoso_configs_v2/spot.cfg",
        "large_tlb": "./config/virtuoso_configs_v2/large_tlb.cfg",
        "ech": "./config/virtuoso_configs_v2/ech.cfg",
    }

    outputs = {
        "pomtlb": "./test_results/pomtlb",
        "dmt": "./test_results/dmt",
        "asap": "./test_results/asap",
        "reservethp": "./test_results/reservethp",
        "revelator": "./test_results/revelator",
        "revelator_open_addressing": "./test_results/revelator_open_addressing",
        "revelator_thp": "./test_results/revelator_thp",
        "spot": "./test_results/spot",
        "large_tlb": "./test_results/large_tlb",
        "ech": "./test_results/ech",
    }

    commands = [
        ("pomtlb", "--perf_model/reserve_thp/target_fragmentation=0.0"),
        ("pomtlb", "--perf_model/reserve_thp/target_fragmentation=0.001"),
        ("pomtlb", "--perf_model/reserve_thp/target_fragmentation=1.0"),
        ("dmt", "--perf_model/reserve_thp/target_fragmentation=0.0"),
        ("dmt", "--perf_model/reserve_thp/target_fragmentation=0.001"),
        ("dmt", "--perf_model/reserve_thp/target_fragmentation=1.0"),
        ("asap", "--perf_model/reserve_thp/target_fragmentation=0.0"),
        ("asap", "--perf_model/reserve_thp/target_fragmentation=0.001"),
        ("asap", "--perf_model/reserve_thp/target_fragmentation=1.0"),
        ("reservethp", "--perf_model/reserve_thp/target_fragmentation=0.0"),
        ("reservethp", "--perf_model/reserve_thp/target_fragmentation=0.001"),
        ("reservethp", "--perf_model/reserve_thp/target_fragmentation=1.0"),
        ("revelator", "--perf_model/revelator/target_fragmentation=1.0"),
        ("revelator", "--perf_model/revelator/target_fragmentation=0.6"),
        ("revelator_open_addressing", "--perf_model/revelator_open/target_fragmentation=1.0"),
        ("revelator_open_addressing", "--perf_model/revelator_open/target_fragmentation=0.6"),
        ("revelator_thp", "--perf_model/revelator_thp/target_thp_fragmentation=0.001"),
        ("revelator_thp", "--perf_model/revelator_thp/target_thp_fragmentation=0.0"),
        ("revelator_thp", "--perf_model/revelator_thp/target_thp_fragmentation=1.0"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=0"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=0.1"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=0.2"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=1"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=2"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=3"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=5"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=50"),
        ("spot", "--perf_model/spot_allocator/target_fragmentation_spot=95"),
        ("large_tlb", "--perf_model/reserve_thp/target_fragmentation=0.0"),
        ("large_tlb", "--perf_model/reserve_thp/target_fragmentation=0.001"),
        ("large_tlb", "--perf_model/reserve_thp/target_fragmentation=1.0"),
        ("ech", "--perf_model/reserve_thp/target_fragmentation=0.0"),
        ("ech", "--perf_model/reserve_thp/target_fragmentation=0.001"),
        ("ech", "--perf_model/reserve_thp/target_fragmentation=1.0")
    ]

    commands = []
    configs_revelator = []
    for hashes in range(1, 4):
        for frag in [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]:
            commands.append(("revelator-frag" + str(frag) + "_h" + str(hashes), "--perf_model/revelator/target_fragmentation=" + str(frag) + " -g --perf_model/revelator/number_of_hashes=" + str(hashes) + " -g --perf_model/revelator/number_of_predictions=" + str(hashes)))


    trace = "/mnt/panzer/kanellok/virtuoso_traces/xs.sift" 

    # Ensure base output dirs exist
    for path in outputs.values():
        os.makedirs(path, exist_ok=True)

    procs = []

    for name, option in commands:
        config_file = configs[name]
        output_root = outputs[name]

        opt_key, opt_val = option.split("=", 1)
        run_dir = os.path.join(output_root, f"frag_{opt_val}_out")
        os.makedirs(run_dir, exist_ok=True)

        # Safe tag for log filenames
        safe_tag = f"{opt_key.replace('/', '_').replace('-', '_')}_{opt_val.replace('.', '_')}"

        stdout_path = os.path.join(run_dir, f"{name}_{safe_tag}.stdout.log")
        stderr_path = os.path.join(run_dir, f"{name}_{safe_tag}.stderr.log")

        cmd = [
            "./run-sniper",
            "-c", config_file,
            "-g",
            option,
            "-d", run_dir,
            "--genstats",
            "-s", f"stop-by-icount:{instructions}",
            f"--traces={trace}",
        ]

        print("Launching:", " ".join(cmd))
        out = open(stdout_path, "wb")
        err = open(stderr_path, "wb")
        p = subprocess.Popen(cmd, stdout=out, stderr=err)
        procs.append((p, out, err, name, option, run_dir, stdout_path, stderr_path))

    # Wait for all processes
    any_fail = False
    for p, out, err, name, option, run_dir, stdout_path, stderr_path in procs:
        ret = p.wait()
        out.close()
        err.close()
        status = "OK" if ret == 0 else f"FAIL({ret})"
        print(f"[{status}] {name} {option}")
        print(f"  dir:    {run_dir}")
        print(f"  stdout: {stdout_path}")
        print(f"  stderr: {stderr_path}\n")
        if ret != 0:
            any_fail = True

    sys.exit(2 if any_fail else 0)

if __name__ == "__main__":
    main()
