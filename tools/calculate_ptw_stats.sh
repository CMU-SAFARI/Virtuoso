#!/bin/bash

python export_line_from_output.py -d $1 -l "PTW.total-PD-latency" > f1
python export_line_from_output.py -d $1 -l "PTW.total-PDPE-latency" > f2
python export_line_from_output.py -d $1 -l "PTW.total-PMLE4-latency" > f3
python export_line_from_output.py -d $1 -l "PTW.total-PT-latency" > f4
python export_line_from_output.py -d $1 -l "PTW.page_walks" > f5

python combine_ptw_cycles.py $2 $3
