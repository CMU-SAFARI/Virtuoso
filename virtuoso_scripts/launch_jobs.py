
import os
import argparse
import sys

# I need the user to provide an argument --native or --slurm to specify the execution mode

parser = argparse.ArgumentParser(
    description="Script creats experiments run in native or SLURM mode.")
parser.add_argument("--native", action="store_true",
                    help="Run in native mode.")
parser.add_argument("--slurm", action="store_true", help="Run in SLURM mode.")
parser.add_argument("path", help="Path to the file or directory.")
parser.add_argument("--excluded_nodes", nargs='?', default=None,
                    help="Comma-separated list of excluded nodes.")

args = parser.parse_args()

if args.native and args.slurm:
    print("Error: Cannot specify both --native and --slurm. Choose one execution mode.")
    exit(1)

slurm = False
native = False
if args.native:
    native = True
elif args.slurm:
    slurm = True
else:
    print("Error: Please specify either --native or --slurm to choose the execution mode.")
    exit(1)

trace_path = "/app/traces/"

traces = [("rnd", "randacc_500M.sift")]


# Docker command to run the binary inside the container
docker_command = "docker run --rm -v "+args.path +":/app/ docker.io/kanell21/artifact_evaluation:victima"

radix = " -c /app/config/virtuoso_configs/virtuoso_baseline.cfg"
hdc = " -c /app/config/virtuoso_configs/virtuoso_hdc.cfg"
ht = " -c /app/config/virtuoso_configs/virtuoso_ht.cfg"
cuckoo = " -c /app/config/virtuoso_configs/virtuoso_ech.cfg"

pomtlb = " -c /app/config/virtuoso_configs/virtuoso_pomtlb.cfg"
midgard = " -c /app/config/virtuoso_configs/virtuoso_midgard.cfg"
ranged = " -c /app/config/virtuoso_configs/virtuoso_rmm.cfg"
utopia = " -c /app/config/virtuoso_configs/virtuoso_utopia.cfg"

configs = [
("radix_no_pref", radix),
("radix_pref", radix+" -g --perf_model/tlb_subsystem/prefetch_enabled=true"+ " -g --/perf_model/tlb_prefetch/pq1=\"asp\""),
("hdc_pref", hdc+" -g --perf_model/tlb_subsystem/prefetch_enabled=true"+ " -g --/perf_model/tlb_prefetch/pq1=\"asp\""),
("ech_pref", cuckoo+" -g --perf_model/tlb_subsystem/prefetch_enabled=true"+ " -g --/perf_model/tlb_prefetch/pq1=\"asp\""),

# ("rmm_buddy", ranged+ " -g --perf_model/virtuos/memory_allocator=\"buddy\" -g --perf_model/pmem_alloc/target_memory=0.0"),
# ("rmm_buddy1", ranged+ " -g --perf_model/virtuos/memory_allocator=\"buddy\" -g --perf_model/pmem_alloc/target_memory=0.1"),
# ("rmm_buddy3", ranged+ " -g --perf_model/virtuos/memory_allocator=\"buddy\" -g --perf_model/pmem_alloc/target_memory=0.3"),
# ("rmm_buddy4", ranged+ " -g --perf_model/virtuos/memory_allocator=\"buddy\" -g --perf_model/pmem_alloc/target_memory=0.4"),
# ("rmm_buddy5", ranged+ " -g --perf_model/virtuos/memory_allocator=\"buddy\" -g --perf_model/pmem_alloc/target_memory=0.5"),
# ("rmm_buddy6", ranged+ " -g --perf_model/virtuos/memory_allocator=\"buddy\" -g --perf_model/pmem_alloc/target_memory=0.6"),

# ("radix_thp_01", radix+ " -g --perf_model/virtuos/memory_allocator=\"simple_thp\" -g --perf_model/pmem_alloc/target_memory=0.0"),
# ("radix_thp_02", radix+ " -g --perf_model/virtuos/memory_allocator=\"simple_thp\" -g --perf_model/pmem_alloc/target_memory=0.1 "),
# ("radix_thp_03", radix+ " -g --perf_model/virtuos/memory_allocator=\"simple_thp\" -g --perf_model/pmem_alloc/target_memory=0.2 "),
# ("radix_thp_04", radix+ " -g --perf_model/virtuos/memory_allocator=\"simple_thp\" -g --perf_model/pmem_alloc/target_memory=0.3 "),
# ("radix_thp_06", radix+ " -g --perf_model/virtuos/memory_allocator=\"simple_thp\" -g --perf_model/pmem_alloc/target_memory=0.4 "),
# ("radix_thp_07", radix+ " -g --perf_model/virtuos/memory_allocator=\"simple_thp\" -g --perf_model/pmem_alloc/target_memory=0.5 ")


# ("hdc", hdc),
# ("ht", ht),
# ("cuckoo", cuckoo),

# ("pomtlb", pomtlb),
# ("midgard", midgard),
# ("ranged", ranged),
# ("utopia", utopia)
]


virtuoso_parameters = "/app/run-sniper -s stop-by-icount:20000000 --genstats --power"

# # # Create the jobfile: a bash script that runs all the binaries with all the configurations
with open("./jobfile", "w") as jobfile:
    jobfile.write("#!/bin/bash\n")

    for (trace_name, trace) in traces:

        for (config_name, configuration_string) in configs:

            trace_command = "--traces={}".format(trace_path+trace)

            output_command = "-d /app/results/{}_{}".format(
                config_name, trace_name)

            if (slurm):
                # SLURM parameters are overprovisioned just in case the simulation takes longer than expected
                if args.excluded_nodes is not None:
                    execution_command = "sbatch --exclude="+args.excluded_nodes+"  -J {}_{} --output=./results/{}_{}.out --error=./results/{}_{}.err docker_wrapper.sh ".format(
                        config_name, trace_name, config_name, trace_name, config_name, trace_name)
                else:
                    execution_command = "sbatch   -J {}_{} --output=./results/{}_{}.out --error=./results/{}_{}.err docker_wrapper.sh ".format(
                        config_name, trace_name, config_name, trace_name, config_name, trace_name)
                command = execution_command + "\"" + docker_command + " " + virtuoso_parameters + \
                    " " + output_command+" "+configuration_string+" "+trace_command+"\""
            elif (native):
                command = docker_command + " " + virtuoso_parameters + " " + output_command+" " + \
                    configuration_string+" "+trace_command + \
                    " > ./results/"+config_name+"_"+trace_name+".out  &"
            # command = docker_command + " " + virtuoso_parameters + " " + output_command+" "+configuration_string+" "+trace_command

            jobfile.write(command)
            jobfile.write("\n")
