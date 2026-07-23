import os
import sys

# Paths and setup
artifact_path = sys.argv[1]
sniper_path = artifact_path  + "simulator/sniper/"
script_path  = sniper_path   + "scripts/experiments_general_script/"
results_path = script_path   + "sniper_motivation_results/"
trace_folder = sniper_path   + "virtuoso_traces/"
os.makedirs(results_path, exist_ok=True)

# Traces used
virtuoso_traces = [
    ("bc", "bc.sift"),
    ("bfs", "bfs.sift"),
    ("cc", "cc.sift"),
    ("tc", "tc.sift"),
    ("gc", "gc.sift"),
    ("pr", "pr.sift"),
    ("sssp", "sssp.sift"),
    ("rnd", "rnd.sift"),
    ("xs", "xs.sift"),
    ("dlrm", "dlrm.sift"),
    ("gen", "gen.sift"),
]

# Swept parameters
radix_config = "-c" + sniper_path + "config/virtuoso_configs/virtuoso_reservethp.cfg "
ech_config   = "-c" + sniper_path + "config/virtuoso_configs/virtuoso_reservethp_ech.cfg "
hdc_config   = "-c" + sniper_path + "config/virtuoso_configs/virtuoso_reservethp_hdc.cfg "
ht_config    = "-c" + sniper_path + "config/virtuoso_configs/virtuoso_reservethp_ht.cfg "

# 1
configs = [ech_config] 
impls = ["ech"]


# 11 traces
# (2800 - 400)/800 + 1 = 4
dram_speeds = list(range(400, 2801, 800))  # 400 to 2800 MTPS 
fragmentations = [0.005, 0.006, 0.008, 0.01] + [0.1, 0.2, 0.5] # 7
cache_kib = 2 ** 11  # 2048 KiB (2 MiB) ; # 1

#  Generate config combinations
merged_configs = []
for config, impl in zip(configs, impls):
    for dram_speed in dram_speeds:
        for frag in fragmentations:
                name = f"{impl}-m{dram_speed}-f{frag}-c{cache_kib}"
                cfg = (
                    config
                    + f" -g --perf_model/dram/ddr/dram_speed={dram_speed} "
                    + f" -g --perf_model/reserve_thp/target_fragmentation={frag} "
                    + f" -g --perf_mode/nuca/cache_size={cache_kib} -g --perf_model/nuca/associativity=16 "
                )
                merged_configs.append((name, cfg))

# Write jobfile
with open(sys.argv[2], "w") as jobfile:
    jobfile.write("#!/bin/bash\n")
    for (trace_name, trace_file) in virtuoso_traces:
        for (config_name, config_string) in merged_configs:
            run_cmd = sniper_path + "run-sniper"
            sniper_params = " --no-cache-warming --genstats -s stop-by-icount:300000000 "
            output_dir = results_path + f"{config_name}_{trace_name}"
            os.makedirs(output_dir, exist_ok=True)
            output_flag = f" -d {output_dir} "
            trace_flag = f" --traces={trace_folder}{trace_file} "
            full_cmd = run_cmd + sniper_params + config_string + output_flag + trace_flag

            sbatch_cmd = (
                f"sbatch --mem=4GB  -J {config_name}_{trace_name} "
                f"--output={output_dir}/slurm.out "
                f"--error={output_dir}/slurm.err "
                f"{sniper_path}native_wrapper.sh "
                f"\" {full_cmd} \"\n"
            )
            jobfile.write(sbatch_cmd)


