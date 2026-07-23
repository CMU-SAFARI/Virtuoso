#!/bin/bash

../record-trace -o minor_fault_microbench_verylow.trace ./minor_fault_microbench  2048 2048 128 20000 sequential
../record-trace -o minor_fault_microbench_low.trace ./minor_fault_microbench  2048 2048 128 2000 sequential
../record-trace -o minor_fault_microbench_mid.trace ./minor_fault_microbench  2048 2048 128 200 sequential
../record-trace -o minor_fault_microbench_high.trace ./minor_fault_microbench  2048 2048 128 20 sequential
../record-trace -o minor_fault_microbench_veryhigh.trace ./minor_fault_microbench  2048 2048 128 2 sequential