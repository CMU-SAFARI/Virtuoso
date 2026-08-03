#!/usr/bin/env python3
"""
Script: count_gates_and_leakage.py

This script performs two tasks:
1. It reads a synthesized Verilog netlist and counts the instances of each gate 
   (cell) type that ends with 'X1' or 'X2'. 
2. It reads a library file to extract the leakage power values for each cell.
   When multiple leakage power values are provided in a cell block,
   the highest value is chosen.

Usage:
    python count_gates_and_leakage.py --verilog <netlist_file.v> --lib <library_file.lib>
"""

import re
import sys
import argparse

def count_gates_from_verilog(file_path):
    """
    Counts gate instances from the Verilog netlist file.
    It only counts lines where the gate type ends in X1 or X2.
    """
    gate_counts = {}
    # Regex pattern:
    # - Start-of-line, optional whitespace.
    # - A token which ends with X1 or X2, then whitespace, then an instance name, 
    #   then optional whitespace and a '('.
    instantiation_pattern = re.compile(r'^\s*([\w\$]+(?:X1|X2))\s+([\w\$]+)\s*\(')
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                match = instantiation_pattern.match(line)
                if match:
                    gate_type = match.group(1)
                    gate_counts[gate_type] = gate_counts.get(gate_type, 0) + 1
    except FileNotFoundError:
        sys.exit(f"Error: Netlist file '{file_path}' not found.")
    
    return gate_counts

def parse_library_file(file_path):
    """
    Parses the library file to extract leakage power values for each cell.
    The library file contains blocks like:
    
       cell (AND2_X1) {
         ...
         leakage_power () {
           when  : "...";
           value : 20.324370;
         }
         leakage_power () {
           when  : "...";
           value : 30.850688;
         }
         ...
       }
       
    For each cell, the script collects all "value" entries (ignoring the context)
    and selects the highest value.
    
    Returns:
        A dict mapping cell type (e.g., "AND2_X1") to the maximum leakage value (float).
    """
    lib_leakage = {}
    current_cell = None
    leakage_values = []
    brace_count = 0

    # Regex to detect the start of a cell block.
    cell_start_re = re.compile(r'^\s*cell\s*\(\s*([\w\$]+)\s*\)\s*{')
    # Regex to catch a leakage value line.
    value_re = re.compile(r'^\s*value\s*:\s*([0-9]*\.?[0-9]+)\s*;')
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                # Check for start of a cell block.
                if current_cell is None:
                    m = cell_start_re.match(line)
                    if m:
                        current_cell = m.group(1)
                        leakage_values = []
                        # We are starting a new cell block; initialize brace count.
                        brace_count = line.count("{") - line.count("}")
                    # Otherwise, ignore line.
                else:
                    # We are inside a cell block.
                    # Update brace count based on the current line.
                    brace_count += line.count("{") - line.count("}")
                    # Look for leakage power "value" lines.
                    m_value = value_re.match(line)
                    if m_value:
                        try:
                            val = float(m_value.group(1))
                            leakage_values.append(val)
                        except ValueError:
                            pass  # ignore values that can't be converted.
                    # If brace_count returns to 0, we have ended the current cell block.
                    if brace_count <= 0:
                        if leakage_values:
                            # Choose the highest leakage value from the collected values.
                            max_leakage = max(leakage_values)
                            lib_leakage[current_cell] = max_leakage
                        else:
                            # Optionally, you could assign a default (or warn) if no leakage value is found.
                            lib_leakage[current_cell] = None
                        current_cell = None
                        leakage_values = []
    except FileNotFoundError:
        sys.exit(f"Error: Library file '{file_path}' not found.")
    
    return lib_leakage

def main():
    parser = argparse.ArgumentParser(
        description="Count gate instances in a Verilog netlist and report leakage power from a library file.")
    parser.add_argument("--verilog", required=True, help="Path to the synthesized Verilog netlist file")
    parser.add_argument("--lib", required=True, help="Path to the library (.lib) file")
    args = parser.parse_args()
    
    # Count the gates in the Verilog netlist.
    gate_counts = count_gates_from_verilog(args.verilog)
    print("Gate Counts from Netlist:")
    for gate, count in sorted(gate_counts.items()):
        print(f"  {gate}: {count}")
    
    # Parse the library file for leakage values.
    leakage_dict = parse_library_file(args.lib)
    print("\nLeakage Power Values from Library File (highest value chosen per cell):")
    for cell, leakage in sorted(leakage_dict.items()):
        if leakage is not None:
            print(f"  {cell}: {leakage} (unit as provided)")
        else:
            print(f"  {cell}: No leakage value found")
    
    # Optionally, cross-reference gate counts with leakage values.
    total_leakage_power = 0
    print("\nEstimated Total Leakage per Gate Type:")
    for gate, count in sorted(gate_counts.items()):
        if gate in leakage_dict and leakage_dict[gate] is not None:
            total_leakage = count * leakage_dict[gate]
            total_leakage_power += total_leakage
            print(f"  {gate}: {count} instance(s) x {leakage_dict[gate]} = {total_leakage}")
        else:
            print(f"  {gate}: {count} instance(s) but leakage value not found in library.")

    print(f"Total leakage power is: {total_leakage_power}nW or {total_leakage_power/1000}uW")

if __name__ == '__main__':
    main()
