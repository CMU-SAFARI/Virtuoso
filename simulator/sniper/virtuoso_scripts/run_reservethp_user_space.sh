#!/bin/bash


# This script runs the Sniper simulator with a specified configuration file and workload.
# Usage: ./run_example.sh 

# Choose the configuration file for the Sniper simulator
CONFIG_FILE=./config/virtuoso_configs_v2/userspace_reservethp.cfg

# Path to your executable
WORKLOAD=/mnt/panzer/kanellok/virtuoso_revelator_feature/virtuoso_artifact/simulator/sniper/traces_victima/bc.sift
OUTPUT=./mimicos/test_output/output
MIMICOS=./mimicos/build/startup_mimicos
MIMICOS_CONFIG=./mimicos/configs/reservethp_32GB.ini
STATS_OUTPUT_FOLDER=./userspace-mimicos/

# 1 Million instructions (TODO @vlnitu: revert to 10Mil after debug)
ICOUNT=1000000

# user-space
./run-sniper -c $CONFIG_FILE -d $STATS_OUTPUT_FOLDER --genstats -s stop-by-icount:$ICOUNT --roi --no-cache-warming -- $MIMICOS $MIMICOS_CONFIG $WORKLOAD $OUTPUT

#Check if the command was successful by looking for sim.stats in the output directory

if [ -f "${STATS_OUTPUT_FOLDER}/sim.stats" ]; then
    echo "Simulation completed successfully. Output is in ${STATS_OUTPUT_FOLDER}."
else
    echo "Simulation failed. Check the configuration and workload."
fi