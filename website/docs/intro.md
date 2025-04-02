---
sidebar_position: 1
---

# Getting Started

Virtuoso is a new simulation framework designed to enable fast and accurate prototyping and evaluation of virtual memory (VM) schemes.  It employs a lightweight userspace kernel, MimicOS, which imitates the desired OS kernel code, allowing researchers to simulate only the relevant OS routines and easily develop new OS modules.  Virtuoso's imitation-based OS simulation methodology facilitates the evaluation of hardware/OS co-designs by accurately modeling the interplay between OS routines and hardware components. 

## Hardware Requirements

- **Architecture**: x86-64 system.
- **Memory**: 4–13 GB of free memory per experiment.
- **Storage**: 10 GB of storage space for the dataset.


## Software Requirements

- Python 3.8 or later (Virtuoso+Sniper can also work with Python 2.7 but we do not recommend it)
- libpython3.8-dev or later (make sure to install the correct version of libpython for your Python version)
- g++


We provide a script to install some potentially missing dependencies. 
The will also install Miniconda, which is a lightweight version of Anaconda so that you can create a virtual environment for Python 3.8 or later.

```bash
cd Virtuoso/simulator/sniper/
sh install_dependencies.sh
bash # start a new bash shell to make sure the environment variables are set
conda create -n virtuoso python=3.10 
conda activate virtuoso # Python 3.10 will be your default Python version in this environment
```
Download some traces from workloads that experienced high address translation overheads. 

```bash
sh download_traces.sh
```
## Getting Started

 
Let's first clean the Sniper simulator and then build it from scratch.

```bash
cd Virtuoso/simulator/sniper
make distclean # clean up so that we can build from scratch
make -j # build Sniper
```

Make sure the simulator is working by running the following command:

```bash
sh run_example.sh 
```

Let's now do something more interesting and run actual experiments. 
We will run the following experiment:

1) Baseline MMU with a Radix page table and a reservation-based THP policy (similar to the one used in FreeBSD) with 4KB and 2MB pages.
2) We will sweep the memory fragmentation ratio to observe the effect of memory fragmentation on the performance of address translation.
3) We will run the experiment multiple translation-intensive workloads.

To run the experiments efficiently, we will use the Slurm job scheduler. 
If you do not have Slurm installed, you can run the experiments by modifying the `create_jobfile_virtuoso_reservethp.py` script to run the experiments sequentially without `sbatch` and `srun`.

```bash
cd Virtuoso/scripts/virtuoso_sniper
python3 create_jobfile_virtuoso_reservethp.py ../../Virtuoso/ ../jobfiles/reservethp.jobfile
cd ../jobfiles
source reservethp.jobfile # This will run the experiments with Slurm
```



