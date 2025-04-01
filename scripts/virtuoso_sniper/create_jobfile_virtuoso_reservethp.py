
import os
import argparse
import sys



artifact_path = sys.argv[1]
sniper_path = artifact_path + "simulator/sniper/"
trace_folder = artifact_path + "simulator/sniper/traces_victima/"


# if trace_folder is empty, download wget https://storage.googleapis.com/traces_virtual_memory/traces_victima then 
# tar -xzf traces_victima and place them in the artifact_path/simulator/sniper/


os.makedirs(artifact_path + "sniper_virtuoso_results", exist_ok=True)

results_path = artifact_path + "sniper_virtuoso_results"

victima_traces = [
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
          ("gen", "gen.sift")
]



baseline = " -c "+sniper_path+"config/virtuoso_configs/virtuoso_reservethp.cfg "

frequency = " -g --perf_model/core/frequency=2.9 "
single_channel = " -g --perf_model/dram/ddr/num_channels=1 "
small_cache = " -g --perf_mode/nuca/cache_size=32768 -g --perf_model/nuca/associativity=16 "
prefetcher_off = " -g --perf_model/l2_cache/prefetcher=none -g --perf_model/l1_dcache/prefetcher=none "




configs_reservethp_radix = [
    ("radix-resthp-frag-0", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.0"),
    ("radix-resthp-frag-2", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.02"),
    ("radix-resthp-frag-4", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.04"),
    ("radix-resthp-frag-6", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.06"),
    ("radix-resthp-frag-8", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.08"),
    ("radix-resthp-frag-10", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.1"),
    ("radix-resthp-frag-12", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.12"),
    ("radix-resthp-frag-14", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.14"),
    ("radix-resthp-frag-16", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.16"),
    ("radix-resthp-frag-18", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.18"),
    ("radix-resthp-frag-20", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.20"),
    ("radix-resthp-frag-22", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.22"),
    ("radix-resthp-frag-24", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.24"),
    ("radix-resthp-frag-26", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.26"),
    ("radix-resthp-frag-28", baseline + "-g --perf_model/reserve_thp_allocator/target_fragmentation=0.28"),
]

configs_oracle = [
    ("no-translation", baseline + "-g --general/translation_enabled=false")

]

configs = configs_reservethp_radix + configs_oracle


# # # Create the jobfile: a bash script that runs all the binaries with all the configurations
counter = 0
with open(sys.argv[2], "w") as jobfile:
    jobfile.write("#!/bin/bash\n")

    for (tracename, trace) in victima_traces:
        for (config_name, configuration_string) in configs:
            
            execution_command = sniper_path+ "run-sniper"
            sniper_parameters = " --no-cache-warming --genstats -s stop-by-icount:5000000000 "
            output_directory = artifact_path + "sniper_virtuoso_results/" + config_name + "_" + tracename
            # create the output directory
            os.makedirs(output_directory, exist_ok=True)
            output_command  = " -d " + output_directory + " "
            tracepath = " --traces=" + trace_folder + trace + " "

            command = execution_command + sniper_parameters + configuration_string + output_command + tracepath
      
            sbatch_addition = ("sbatch --mem=16GB  -J {}_{} --output="+output_directory+"/slurm.out --error="+output_directory+"/slurm.err "+sniper_path+"native_wrapper.sh ").format(config_name, tracename, config_name, tracename, config_name, tracename)
            sbatch_addition
            
            jobfile.write(sbatch_addition + "\"" + " " + command + "\"")
            jobfile.write("\n")

            counter+=1


        jobfile.write("\n")