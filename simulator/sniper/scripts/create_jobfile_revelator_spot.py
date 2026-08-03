
# Source: https://github.com/CMU-SAFARI/Virtuoso/blob/main/scripts/virtuoso_sniper/create_jobfile_virtuoso_reservethp.py
import os
import argparse
import sys



artifact_path = sys.argv[1]
sniper_path = artifact_path + "simulator/sniper/"
trace_folder = artifact_path + "simulator/sniper/virtuoso_traces/"


os.makedirs(artifact_path + "sniper_virtuoso_spot_results/", exist_ok=True)

results_path = artifact_path + "sniper_virtuoso_spot_results/"

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
        #   ("kcore", "kcore.sift"),
]



spot = " -c "+ sniper_path + "config/virtuoso_configs/virtuoso_spot.cfg "

frequency = " -g --perf_model/core/frequency=2.9 "
single_channel = " -g --perf_model/dram/ddr/num_channels=1 "
small_cache = " -g --perf_mode/nuca/cache_size=32768 -g --perf_model/nuca/associativity=16 "
prefetcher_off = " -g --perf_model/l2_cache/prefetcher=none -g --perf_model/l1_dcache/prefetcher=none "

configs_reservethp_radix = [
    ("radix-spot-frag-0",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=0"),
    ("radix-spot-frag-1",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=10"),
    ("radix-spot-frag-2",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=20"),
    ("radix-spot-frag-3",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=30"),
    ("radix-spot-frag-4",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=40"),
    ("radix-spot-frag-5",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=50"),
    ("radix-spot-frag-6",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=60"),
    ("radix-spot-frag-7",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=70"),
    ("radix-spot-frag-8",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=80"),
    ("radix-spot-frag-9",  spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=90"),
    ("radix-spot-frag-10", spot  + "-g --perf_model/spot_allocator/target_fragmentation_spot=100"),
]

configs_oracle = [
    ("no-translation", spot + "-g --general/translation_enabled=false ")

]

configs = configs_reservethp_radix + configs_oracle


# # # Create the jobfile: a bash script that runs all the binaries with all the configurations
counter = 0
with open(sys.argv[2], "w") as jobfile:
    jobfile.write("#!/bin/bash\n")

    for (tracename, trace) in virtuoso_traces:
        for (config_name, configuration_string) in configs:
            
            execution_command = sniper_path+ "run-sniper "
            sniper_parameters = " --no-cache-warming --genstats -s stop-by-icount:300000000 " # 300M instructions
            output_directory = artifact_path + "sniper_virtuoso_spot_results/" + config_name + "_" + tracename
            # create the output directory
            os.makedirs(output_directory, exist_ok=True)
            output_command  = " -d " + output_directory + " "
            tracepath = " --traces=" + trace_folder + trace + " "

            command = execution_command + sniper_parameters + configuration_string + output_command + tracepath
      
            sbatch_addition = ("sbatch --mem=4GB  -J {}_{} --output="+output_directory+"/slurm.out --error="+output_directory+"/slurm.err "+sniper_path+"native_wrapper.sh ").format(config_name, tracename, config_name, tracename, config_name, tracename)
            sbatch_addition
            
            jobfile.write(sbatch_addition + "\"" + " " + command + "\"")
            jobfile.write("\n")

            counter+=1


        jobfile.write("\n")
