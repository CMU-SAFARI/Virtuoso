#!/bin/bash


# This script runs the Sniper simulator with a specified configuration file and workload.
# Usage: ./run_example.sh 

CONFIG_FILE=./config/virtuoso_configs/virtuoso_baseline.cfg

WORKLOAD=ls

./run-sniper -c $CONFIG_FILE -d ./example_output --genstats -- $WORKLOAD


#Check if the command was successful by looking for sim.stats in the output directory

if [ -f ./example_output/sim.stats ]; then
    echo "Simulation completed successfully. Output is in ./example_output."
else
    echo "Simulation failed. Check the configuration and workload."
fi

rm -rf ./example_output
