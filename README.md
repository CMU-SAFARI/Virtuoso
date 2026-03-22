# Virtuoso: Fast and Accurate Virtual Memory Research via Imitation-based OS Simulation

[![ASPLOS 2025](https://img.shields.io/badge/ASPLOS-2025-blue)](https://arxiv.org/pdf/2403.04635v2)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Virtual memory is a significant performance bottleneck across modern workloads. Researchers need tools to explore new hardware/OS co-designs that optimize virtual memory across diverse applications and systems. Existing tools either lack accuracy in modeling OS software components or are too slow for prototyping designs that span the hardware/software boundary.

**Virtuoso** addresses this challenge through an *imitation-based OS simulation methodology*. At its core is **MimicOS**, a lightweight userspace OS kernel that imitates only the necessary kernel functionalities (e.g., physical memory allocation, page fault handling). MimicOS accelerates simulation compared to full-system OS simulation while providing accessible high-level programming interfaces for developing new OS memory management routines, enabling flexible and precise evaluation of virtual memory's application-level and system-level effects.

Virtuoso integrates with diverse architectural simulators, each specializing in different system design aspects. It currently supports [Sniper](https://github.com/snipersim/snipersim) (event-driven CPU simulation) and [Ramulator2](https://github.com/CMU-SAFARI/ramulator2) (cycle-accurate DRAM timing), with a shared MimicOS core ensuring consistent OS behavior across simulation backends.

> Konstantinos Kanellopoulos, Konstantinos Sgouras, F. Nisa Bostanci, Andreas Kosmas Kakolyris, Berkin Kerim Konar, Rahul Bera, Mohammad Sadrosadati, Rakesh Kumar, Nandita Vijaykumar, and Onur Mutlu, "Virtuoso: Enabling Fast and Accurate Virtual Memory Research via an Imitation-based Operating System Simulation Methodology," **ASPLOS 2025**. [[Paper]](https://arxiv.org/pdf/2403.04635v2)

## Table of Contents

- [Key Features](#key-features)
- [Repository Structure](#repository-structure)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Configuration System](#configuration-system)
- [Experiment Framework](#experiment-framework)
- [Ramulator2 Integration](#ramulator2-integration)
- [Smoke Tests](#smoke-tests)
- [Website and Documentation](#website-and-documentation)
- [Citation](#citation)
- [Contributing](#contributing)
- [License](#license)

## Key Features

### MimicOS: Imitation-based OS Kernel
MimicOS is a lightweight userspace kernel that imitates the OS memory management subsystem. Rather than running a full Linux kernel, MimicOS provides only the routines required for virtual memory research: physical memory allocation, page fault handling, huge page management, and swap. It uses a policy-based template design so the same allocator and OS service implementations work across Sniper, Ramulator2, and future simulator integrations.

### Physical Memory Allocators
| Allocator | Description |
|-----------|-------------|
| **Baseline** | Simple 4KB buddy-based page allocator |
| **ReserveTHP** | Reservation-based transparent huge pages (4KB + 2MB) with configurable fragmentation |
| **SpOT** | Contiguity-aware allocator exploiting OS allocation patterns ([Alverti et al., ISCA '20](https://chloe-alverti.github.io/publications/isca2020-contiguity/)) |
| **ASAP** | Prefetched address translation with aggressive superpage allocation ([Margaritov et al., MICRO '19](https://ustiugov.github.io/assets/files/ASAP_MICRO19.pdf)) |
| **Utopia** | Restricted segments (RestSegs) with direct VA-to-PA computation ([Kanellopoulos et al., MICRO '23](https://dl.acm.org/doi/10.1145/3613424.3623789)) |
| **EagerPaging** | Contiguous physical range allocation for entire VMAs, used by RMM ([Karakostas et al., ISCA '15](https://dl.acm.org/doi/10.1145/2872887.2749471)) |
| **NUMA ReserveTHP** | Multi-node reservation-based THP with per-node capacity and placement policies |
| **Buddy** | Power-of-two buddy system allocator (shared foundation for all allocators) |

### Page Table Formats
| Page Table | Description |
|------------|-------------|
| **4-Level Radix** | Standard x86-64 radix page table (PML4/PDPT/PD/PT) |
| **Elastic Cuckoo Hash (ECH)** | Cuckoo hashing with elastic bucket resizing ([Skarlatos et al., ASPLOS '20](https://dl.acm.org/doi/10.1145/3373376.3378493)) |
| **Hash Don't Cache (HDC)** | Open-addressing hash table with linear probing and dynamic resizing ([Yaniv and Tsafrir, SIGMETRICS '16](https://dl.acm.org/doi/10.1145/2964791.2901456)) |
| **Hash Table Chaining** | Chained hash table with dynamic resizing |
| **Range Table** | B-tree based range translations for contiguous mappings ([Karakostas et al., ISCA '15](https://dl.acm.org/doi/10.1145/2872887.2749471)) |

### MMU Designs
| MMU | Description |
|-----|-------------|
| **Base** | Configurable multi-level TLB hierarchy with page walk caches and large page prediction |
| **Spec** | Parallel speculative and conventional page walks ([Barr et al., ISCA '11](https://dl.acm.org/doi/10.1145/2024723.2000101)) |
| **POM-TLB** | Part-of-Memory TLB with software-managed large TLB in DRAM ([Ryoo et al., ISCA '17](https://ieeexplore.ieee.org/document/8192494)) |
| **Range/RMM** | Range Lookup Buffer for contiguous translations with eager paging ([Karakostas et al., ISCA '15](https://dl.acm.org/doi/10.1145/2872887.2749471)) |
| **DMT** | Direct Memory Translation for virtualized clouds ([Zhang et al., ASPLOS '24](https://dl.acm.org/doi/10.1145/3620665.3640358)) |
| **Utopia** | RestSeg walker with CATS prediction and page migration ([Kanellopoulos et al., MICRO '23](https://dl.acm.org/doi/10.1145/3613424.3623789)) |
| **HW Fault** | Hardware page fault handler with delegated memory pool |
| **Virt** | Nested MMU for two-dimensional guest-to-host address translation |

### TLB Prefetchers
| Prefetcher | Description |
|------------|-------------|
| **Agile/ATP** | Adaptive multi-stride TLB prefetcher with frequency detection ([Vavouliotis et al., ISCA '21](https://dl.acm.org/doi/10.1109/ISCA52012.2021.00016)) |
| **Recency** | Pointer-table based recency-aware TLB prefetcher |
| **Distance** | Distance-indexed prediction table for irregular access patterns |
| **Stride** | Classic next-page stride TLB prefetcher |
| **H2** | History-based TLB prefetcher |
| **ASP** | PC-indexed arbitrary stride prefetcher |

### Speculative Translation Engines
| Engine | Description |
|--------|-------------|
| **SpOT** | Offset-based speculation exploiting physical contiguity ([Alverti et al., ISCA '20](https://chloe-alverti.github.io/publications/isca2020-contiguity/)) |
| **Oracle** | Perfect speculation for upper-bound analysis |
| **SpecTLB** | Speculative address translation mechanism ([Barr et al., ISCA '11](https://dl.acm.org/doi/10.1145/2024723.2000101)) |
| **ASAP** | Speculative translation via prefetched address translation ([Margaritov et al., MICRO '19](https://ustiugov.github.io/assets/files/ASAP_MICRO19.pdf)) |

### Additional Features
- **CHiRP**: Control-flow history reuse prediction for dead-entry aware cache replacement ([Mirbagher-Ajorpaz et al., MICRO '20](https://ieeexplore.ieee.org/document/9251943/))
- **MPLRU**: Metadata-Priority LRU adaptive cache controller with multi-armed bandit tuning
- **HugeTLBfs and Swap Cache**: Simulator-agnostic templates in `sniper/include/`
- **Multicore support**: 2, 4, 8, and 16 core configurations with shared TLB hierarchies
- **NUMA support**: Multi-node topologies (2-node/8-core, 4-node/16-core)
- **CXL memory tiers**: Configurable CXL-attached memory with ASIC/FPGA/tiered latency models
- **Ramulator2 integration**: MimicOS IPC bridge for cycle-accurate DRAM timing during page walks
- **Comprehensive experiment framework**: YAML-driven configs, jobfile generation, SLURM submission, and smoke tests

## Repository Structure

```
Virtuoso/
├── mimicos/                     # Shared MimicOS userspace kernel
│   ├── include/mm/              # Allocator factory, radix page table headers
│   └── src/                     # MimicOS core, page table, allocator implementations
│       ├── mimicos/             # MimicOS kernel runtime
│       ├── page_table/          # Page table implementations
│       ├── physical_allocator/  # Physical memory allocator implementations
│       ├── metrics/             # Performance counters and telemetry
│       └── inih/                # INI config parser
│
├── simulator/
│   ├── sniper/                  # Sniper multi-core simulator (trace-driven)
│   │   ├── common/              # Core simulation code
│   │   │   └── core/memory_subsystem/
│   │   │       └── parametric_dram_directory_msi/
│   │   │           ├── mmu_designs/           # MMU implementations
│   │   │           ├── spec_engine_designs/   # Speculative engine implementations
│   │   │           └── translation_components/
│   │   │               ├── tlb.cc/h                   # TLB model
│   │   │               ├── tlb_subsystem.cc           # TLB hierarchy
│   │   │               └── tlb_prefetching/           # TLB prefetcher implementations
│   │   ├── include/
│   │   │   └── memory_management/
│   │   │       ├── physical_memory_allocators/  # Simulator-agnostic allocator templates
│   │   │       ├── hugetlbfs.h                  # HugeTLBfs template
│   │   │       ├── swap_cache.h                 # Swap cache template
│   │   │       └── numa/                        # NUMA topology headers
│   │   ├── config/                              # Modular configuration files
│   │   │   ├── address_translation_schemes/     # Top-level composed configs
│   │   │   │   └── multicore/                   # Multi-core variants
│   │   │   ├── core_configs/                    # CPU core models
│   │   │   ├── mmu_configs/                     # MMU design configs
│   │   │   ├── pagetable_configs/               # Page table format configs
│   │   │   ├── physical_memory_allocators/      # Allocator configs
│   │   │   ├── spec_engine_configs/             # Speculative engine configs
│   │   │   ├── dram_configs/                    # DRAM timing configs
│   │   │   ├── prefetcher_configs/              # Data prefetcher configs
│   │   │   ├── cxl_configs/                     # CXL memory tier configs
│   │   │   └── common_configs/                  # Base system configs
│   │   └── testing/
│   │       └── test_config.yaml                 # Smoke test definitions
│   └── ramulator2/              # Ramulator2 DRAM simulator (submodule)
│
├── experiments/                 # Experiment framework
│   ├── clist.yaml               # Central experiment configuration
│   ├── create_experiments.py    # Jobfile generator
│   ├── safe_submit.py           # Validated SLURM submission
│   ├── get_experiments_status.py # Experiment monitoring
│   ├── create_rerun_experiments.py # Failed experiment re-launcher
│   └── vm_tlist/                # Trace list files
│
├── patches/                     # Integration patches
│   ├── apply_ramulator2_mimicos.sh  # Ramulator2 + MimicOS patch script
│   └── ramulator2_mimicos.patch     # The patch file
│
├── vma_infer/                   # VMA inference tool (infers VMAs from traces)
├── tools/                       # Analysis and mini-simulation tools
└── docs/                        # Documentation
    └── ramulator2_mimicos.md    # Ramulator2 + MimicOS integration guide
```

## Prerequisites

### Hardware
- **Architecture**: x86-64 system
- **Memory**: 4 to 13 GB free memory per experiment
- **Storage**: 10 GB for traces and build artifacts

### Software
- Python 3.8+ (3.10 recommended)
- `libpython3-dev` (matching your Python version)
- g++ (with C++17 support)
- GNU Make
- PyYAML (`pip install pyyaml`)
- SLURM (optional, for cluster job submission)

## Quick Start

### 1. Clone the Repository

```bash
git clone --recursive https://github.com/CMU-SAFARI/Virtuoso.git
cd Virtuoso
```

If you already cloned without `--recursive`:
```bash
git submodule update --init --recursive
```

### 2. Install Dependencies

```bash
cd simulator/sniper/
sh install_dependencies.sh
bash  # reload environment

# Optional: create a conda environment
conda create -n virtuoso python=3.10
conda activate virtuoso
```

### 3. Build Sniper

```bash
cd simulator/sniper
make distclean  # clean previous builds
make -j         # build Sniper
```

### 4. Download Traces

```bash
cd simulator/sniper/
sh download_traces.sh
```

### 5. Run a Basic Simulation

```bash
cd simulator/sniper/
sh run_example.sh
```

This runs a baseline simulation with the 4-level radix page table and ReserveTHP allocator on a sample trace.

### 6. Run Smoke Tests

Run the comprehensive smoke test suite (22 configurations, 2M instructions each):

```bash
cd experiments/
python3 create_experiments.py \
  --artifact-path /path/to/Virtuoso \
  --yaml clist.yaml \
  --suite smoke-test-all \
  --suite-dir-name exp_smoke_test_all \
  --force
```

Then submit (or run locally):
```bash
python3 safe_submit.py exp_smoke_test_all/jobfile.sh --dry-run  # validate first
python3 safe_submit.py exp_smoke_test_all/jobfile.sh             # submit to SLURM
```

## Configuration System

Virtuoso uses a modular, composable configuration system. Each top-level address translation scheme is assembled from independent config modules via `#include` directives:

```
address_translation_schemes/<scheme>.cfg
    #include common_configs/base_system_sniperspace.cfg   # Simulation mode
    #include core_configs/meteor_lake_pcore.cfg            # CPU core model
    #include mmu_configs/mmu_base.cfg                      # MMU design
    #include pagetable_configs/4_level_radix.cfg            # Page table format
    #include dram_configs/ddr4_2400.cfg                     # DRAM timing
    #include physical_memory_allocators/reserve_thp.cfg     # Memory allocator
```

### Config Module Directories

| Directory | Purpose | Examples |
|-----------|---------|---------|
| `common_configs/` | Simulation mode (Sniper-space vs. userspace MimicOS) | `base_system_sniperspace.cfg`, `base_system_userspace.cfg` |
| `core_configs/` | CPU core microarchitecture | `meteor_lake_pcore.cfg`, `beefy.cfg`, `wimpy.cfg` |
| `mmu_configs/` | MMU design selection and TLB hierarchy | `mmu_base.cfg`, `mmu_spec.cfg`, `mmu_utopia.cfg`, `mmu_pomtlb.cfg` |
| `pagetable_configs/` | Page table format | `4_level_radix.cfg`, `elastic_cuckoo_hash_table_asplos2020.cfg` |
| `physical_memory_allocators/` | Physical memory allocation policy | `reserve_thp.cfg`, `spot.cfg`, `utopia.cfg`, `eager_paging.cfg` |
| `dram_configs/` | DRAM timing model | `ddr4_2400.cfg`, `ddr5_4800.cfg`, `ddr5_6400.cfg` |
| `spec_engine_configs/` | Speculative translation engine | `spec_engine_spot.cfg`, `spec_engine_oracle.cfg` |
| `prefetcher_configs/` | Data cache prefetcher | `ip_stride_l1.cfg`, `meteor_lake_prefetch.cfg` |
| `cxl_configs/` | CXL-attached memory tiers | `cxl.cfg`, `cxl-tiered.cfg` |

To create a new scheme, compose the desired modules:
```cfg
# My custom scheme: Utopia allocator + ECH page table + SpecTLB engine
#include ./common_configs/base_system_sniperspace.cfg
#include ./core_configs/meteor_lake_pcore.cfg
#include ./mmu_configs/mmu_spec.cfg
#include ./pagetable_configs/elastic_cuckoo_hash_table_asplos2020.cfg
#include ./dram_configs/ddr4_2400.cfg
#include ./physical_memory_allocators/utopia.cfg
#include ./spec_engine_configs/spec_engine_spectlb.cfg
```

## Experiment Framework

The `experiments/` directory provides an end-to-end workflow for running large-scale studies on a SLURM cluster.

### Central Configuration: `clist.yaml`

All experiments are defined in a single YAML file:

```yaml
# Trace suites: named collections of .tlist files
trace_suite:
  top250:
    tracelist_base_path: "vm_tlist/"
    tracelists:
      - top250_cvp1.tlist
      - top250_gap.tlist
      - top250_google.tlist

# Configs: composable simulation configurations
configs:
  configs_base_path: "../simulator/sniper/config/address_translation_schemes/"
  reservethp-full-fragmentation:
    name: "reservethp-frag0.0"
    file: "reservethp.cfg"
    extends:
      - "--perf_model/reserve_thp/target_fragmentation=0.0"
    description: "ReserveTHP with fully fragmented memory"

# Experiment suites: combine traces + configs + instruction count
experiment_suites:
  my-experiment:
    description: "ReserveTHP fragmentation sweep"
    configs: [reservethp-full-fragmentation, reservethp-no-fragmentation]
    trace_suite: top250
    instruction_count: 300000000
```

### Workflow

**Step 1: Generate jobfile**
```bash
python3 experiments/create_experiments.py \
  --artifact-path /path/to/Virtuoso \
  --yaml experiments/clist.yaml \
  --suite my-experiment \
  --suite-dir-name my-experiment \
  --force
```
This creates `experiments/exp_my-experiment/` with `jobfile.sh`, trace/config/job CSVs, and a `results/` directory.

**Step 2: Validate and submit**
```bash
python3 experiments/safe_submit.py experiments/exp_my-experiment/jobfile.sh \
  --max-slurm-jobs 500 \
  --slurm-retry-delay 60
```

`safe_submit.py` validates that Sniper is compiled, debug flags are off, traces exist, configs exist, and disk space is sufficient before submitting.

**Step 3: Monitor progress**
```bash
python3 experiments/get_experiments_status.py \
  --exp-dir experiments/exp_my-experiment
```

**Step 4: Rerun failures**
```bash
python3 experiments/create_rerun_experiments.py \
  --exp-dir experiments/exp_my-experiment \
  --status experiments/exp_my-experiment/status/error.csv \
  --jobfile experiments/exp_my-experiment/jobfile_rerun.sh
```

### Parameter Sweeps

Configs support parameter sweeps for systematic exploration:

```yaml
# Zipped sweep: paired fragmentation + promotion threshold values
reservethp_sweep:
  name: "reservethp"
  file: "reservethp.cfg"
  sweeps:
    - identifier: ["frag", "promotionthresh"]
      option: ["--perf_model/reserve_thp/target_fragmentation=",
               "--perf_model/reserve_thp/threshold_for_promotion="]
      values: [[0.0, 0.25, 0.5, 0.75, 1.0],
               [0.0, 0.0, 0.0, 0.0, 0.0]]
```

Multiple sweep entries produce a Cartesian product across dimensions.

## Ramulator2 Integration

Virtuoso integrates with [Ramulator2](https://github.com/CMU-SAFARI/ramulator2) for cycle-accurate DRAM timing modeling during page table walks. MimicOS communicates with Ramulator2 via an IPC bridge using pipes and the Intel SDE pintool.

For setup instructions, configuration, and architecture details, see [docs/ramulator2_mimicos.md](docs/ramulator2_mimicos.md).

Quick setup:
```bash
# Apply the MimicOS integration patch to Ramulator2
bash patches/apply_ramulator2_mimicos.sh

# Build Ramulator2
cd simulator/ramulator2 && mkdir -p build && cd build && cmake .. && make -j

# Build MimicOS standalone
cd simulator/ramulator2/mimicos
make SNIPER_INCLUDE=$PWD/../../sniper/include
```

## Smoke Tests

The `smoke-test-all` suite validates 22 configurations with 2M instructions on a quick trace. These configurations cover the major axes of the framework:

| Category | Configurations |
|----------|---------------|
| **Allocators** | `no-translation`, `reservethp` (frag 0.0), `reservethp` (frag 1.0), `spot`, `utopia`, `asap` |
| **TLB Prefetchers** | `atp-baseline`, `dp-baseline`, `recency-baseline` |
| **Speculative Engines** | `spec-oracle`, `spectlb` |
| **MMU Designs** | `pomtlb`, `dmt`, `rmm` |
| **Page Tables** | `ech-baseline`, `hdc-baseline`, `ht-baseline`, `reservethp-ech`, `reservethp-hdc`, `reservethp-ht` |
| **Userspace MimicOS** | `userspace-baseline`, `userspace-reservethp` |

Run smoke tests individually by category:
```bash
# Allocators only
--suite smoke-test-allocators

# Prefetchers only
--suite smoke-test-prefetchers

# Page table variants only
--suite smoke-test-page-tables

# MMU designs only
--suite smoke-test-mmu-designs

# Speculative engines only
--suite smoke-test-spec-engines

# Userspace MimicOS only
--suite smoke-test-userspace
```

## Website and Documentation

- **Website**: [https://cmu-safari.github.io/Virtuoso](https://cmu-safari.github.io/Virtuoso) -tutorials, documentation, and API reference
- **Experiment workflow**: [experiments/README.md](experiments/README.md) -detailed experiment framework documentation
- **Ramulator2 integration**: [docs/ramulator2_mimicos.md](docs/ramulator2_mimicos.md)

## Citation

```bibtex
@inproceedings{kanellopoulos2025virtuoso,
    title={{Virtuoso: Enabling Fast and Accurate Virtual Memory Research
            via an Imitation-based Operating System Simulation Methodology}},
    author={Konstantinos Kanellopoulos and Konstantinos Sgouras and
            F. Nisa Bostanci and Andreas Kosmas Kakolyris and
            Berkin K. Konar and Rahul Bera and Mohammad Sadrosadati and
            Rakesh Kumar and Nandita Vijaykumar and Onur Mutlu},
    year={2025},
    booktitle={ASPLOS}
}
```

## Contributing

We welcome contributions. Please open an issue on GitHub to discuss potential changes or report bugs. Pull requests for new allocators, MMU designs, page tables, or TLB prefetchers are especially welcome. The modular architecture makes it straightforward to add new components.

## License

Virtuoso is released under the MIT License, consistent with the Sniper simulator.

## Contact

For questions, please contact:
- Konstantinos Kanellopoulos (<konkanello@gmail.com>)
- Konstantinos Sgouras (<sgouraskon@gmail.com>)
