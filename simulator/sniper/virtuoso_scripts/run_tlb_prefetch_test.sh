#!/bin/bash


# This script runs the Sniper simulator with a specified configuration file and workload.
# Usage: ./run_example.sh 

# Choose the configuration file for the Sniper simulator
CONFIG_FILE=./config/virtuoso_configs_v2/sniperspace_reservethp_tlb_prefetch.cfg

# Path to your executable
WORKLOAD=/mnt/panzer/kanellok/virtuoso_revelator_branch/virtuoso_artifact/simulator/sniper/traces_victima/xs.sift
STATS_OUTPUT_FOLDER=./tlb_prefetch_sniperspace_results


# sniper-space
./run-sniper -c $CONFIG_FILE -d $STATS_OUTPUT_FOLDER -s stop-by-icount:2000000 --genstats --traces=$WORKLOAD 

#Check if the command was successful by looking for sim.stats in the output directory

if [ -f "${STATS_OUTPUT_FOLDER}/sim.stats" ]; then
    echo "Simulation completed successfully. Output is in ${STATS_OUTPUT_FOLDER}."
else
    echo "Simulation failed. Check the configuration and workload."
fi
