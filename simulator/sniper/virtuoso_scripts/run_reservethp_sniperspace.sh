#!/bin/bash


# This script runs the Sniper simulator with a specified configuration file and workload.
# Usage: ./run_example.sh 

# Choose the configuration file for the Sniper simulator
CONFIG_FILE=./config/virtuoso_configs_v2/baseline_reservethp.cfg

# Path to your executable
WORKLOAD=/mnt/panzer/kanellok/virtuoso_revelator_feature/virtuoso_artifact/simulator/sniper/traces_victima/bc.sift
STATS_OUTPUT_FOLDER=./sniperspace_reservethp/


./run-sniper -c $CONFIG_FILE -d $STATS_OUTPUT_FOLDER -s stop-by-icount:1000000 --traces=$WORKLOAD 
# Uncomment the following line if you want to test with real workloads
# ./run-sniper -c $CONFIG_FILE -d ./example_output --genstats -s stop-by-icount:1000000 -- $WORKLOAD 



#Check if the command was successful by looking for sim.stats in the output directory

if [ -f "${STATS_OUTPUT_FOLDER}/sim.stats" ]; then
    echo "Simulation completed successfully. Output is in ${STATS_OUTPUT_FOLDER}."
else
    echo "Simulation failed. Check the configuration and workload."
fi
