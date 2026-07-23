# Reproducability

## Build

Use the docker file `sbt_builder` and build the image

## Generating Verilog

In order to generate the verilog from the chisel code, do:
```
sbt run
```

This will generate the verilog at generated/SpeculationEngine.v

## Synthesis

1. Install yosys

We recommend following the instructions [here](https://github.com/YosysHQ/oss-cad-suite-build#installation)

2. Launch a OSS CAD Suite session
```
source oss-cad-suite/environment
```

3. Launch synthesis and generate the synthesized netlist. 
```
yosys -p "
            read_liberty -lib myLib.lib;
            read_verilog -sv generated/SpeculationEngine.v; 
            proc; fsm; opt; memory; opt; techmap; opt;
            synth -top SpeculationEngine; 
            dfflibmap -liberty myLib.lib;
            abc -liberty myLib.lib -fast;
            read_liberty -lib myLib.lib; 
            write_verilog mapped.v; 
            stat -liberty myLib.lib"
```
This reports the area numbers. 

4. Use a python script to report the static power. 
```
python3 static_power_analyzer.py --verilog synthesized_netlist.v --lib myLib.lib 
```