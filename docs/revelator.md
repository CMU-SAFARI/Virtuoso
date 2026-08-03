# Revelator: Hash-Based Speculative Address Translation

> Konstantinos Kanellopoulos et al., **ISCA 2026**.

Revelator is a speculative translation engine for Virtuoso. Instead of speculating
from physical contiguity (SpOT) or from a prefetched translation (ASAP), Revelator
derives a candidate physical frame directly from the virtual address by hashing it,
and validates the guess against the page table walk that proceeds in parallel. The
guess is only correct if the physical memory allocator placed the page where the
hash says it should be — so Revelator is a **co-design**: a hash-based physical
memory allocator in MimicOS plus a speculation engine in the MMU that replays the
same hash.

This page is the map of what ships in this branch. It is a component inventory, not
an artifact-evaluation script: there is no one-command reproduction harness for the
ISCA '26 numbers here.

---

## Components

### Speculation engines (`spec_engine_designs/`)

Selected via `[perf_model/mmu/spec] type = "..."`.

| `type` | Class | File | Notes |
|--------|-------|------|-------|
| `revelator` | `Revelator` | `revelator.{cc,h}` | Base engine: N hash functions, N predictions, optional Bloom-filter and oracle modes |
| `revelator_open_addressing` | `RevelatorOpenAddressingEngine` | `revelator_open_addressing_engine.{cc,h}` | Open-addressing / linear-probing variant |
| `revelator_thp` | `RevelatorTHP` | `revelator_thp_engine.{cc,h}` | Transparent-huge-page-aware variant |
| `numa_revelator` | `NumaRevelator` | `numa_revelator.{cc,h}` | Multi-node variant with per-node placement |

Registered in `spec_engine_designs/spec_engine_factory.h`.

### Physical memory allocators

Selected via the allocator type string in the allocator config.

| `allocator_type` | Template | Header | Policy |
|------------------|----------|--------|--------|
| `revelator` | `RevelatorAllocator` | `include/memory_management/physical_memory_allocators/revelator.h` | `policies/revelator_policy.h` |
| `revelator_thp` | `RevelatorTHPAllocator` | `.../revelator_thp.h` | `policies/revelator_thp_policy.h` |
| `revelator_simple` | `RevelatorSimpleAllocator` | `.../revelator_simple.h` | `policies/revelator_simple_policy.h` |
| `numa_revelator` | `NumaRevelatorAllocator` | `.../numa_revelator.h` | `policies/numa_revelator_policy.h` |

Shared types live in `.../physical_memory_allocators/revelator_types.h`. Registered
in `common/system/memory_management/physical_memory_allocators/allocator_factory.h`.

### Exception handler

`[general] exception_handler_type = "revelator"` selects `RevelatorExceptionHandler`
(`common/system/memory_management/exception_handling/revelator_exception_handler.{cc,h}`),
which routes page faults through the hash-based placement path. Registered in
`exception_handling/exception_handler_factory.h`.

### Auxiliary material (`revelator_auxiliary/`)

- `RTL/` — Chisel-to-Verilog flow for the hardware speculation engine, plus
  `static_power_analyzer.py` for post-synthesis static power. **Note:** the README
  there describes an `sbt_builder` Docker image and a Chisel source tree that are
  *not* checked into this branch; only the synthesis/analysis instructions and the
  power script ship here.
- `revelator_microbench_mapping/` — native microbenchmarks (`hog_memory.c`,
  `vm_scan.c`) plus `parse_vapa_mapping.py` to extract real VA→PA mappings from a
  Linux host, used to characterise the placement that Revelator's hash has to match.

---

## Configurations

### Single-core address-translation schemes

`simulator/sniper/config/address_translation_schemes/`

| Config | What it composes |
|--------|------------------|
| `revelator.cfg` | Revelator allocator + `mmu_spec` + 4-level radix + range table + swap space |
| `revelator_thp.cfg` | THP-aware Revelator allocator + engine |
| `revelator_open.cfg` | Open-addressing engine + `revelator_alloc_open.cfg` |
| `revelator_ech.cfg` | Revelator over an elastic-cuckoo-hash page table |
| `revelator_hdc.cfg` | Revelator over a Hash-Don't-Cache page table |
| `revelator_virt.cfg` | Nested/virtualized MMU with Revelator |
| `parametric_revelator_4kb.cfg` | 4KB-only, `parametric_dram_directory_msi` protocol |
| `fast_detailed_revelator_4kb.cfg` | 4KB-only, `fast_detailed` caching protocol (faster) |

Multicore variants (2/4/8/16 cores) of the last two live in
`address_translation_schemes/multicore/`.

### Engine and allocator modules

| Directory | Files |
|-----------|-------|
| `spec_engine_configs/` | `spec_engine_revelator.cfg`, `_lean`, `_open`, `_simple`, `_thp`, plus `revelator_engine_params.cfg` |
| `physical_memory_allocators/` | `revelator_alloc.cfg`, `revelator_alloc_open.cfg`, `revelator_simple_alloc.cfg`, `revelator_thp_alloc.cfg`, `numa_revelator_alloc.cfg` |
| `core_configs/` | `revelator_core.cfg` |
| `virtuoso_configs/` | `revelator_virt.cfg` |

`revelator_engine_params.cfg` exists so the engine can run **standalone** over a
non-Revelator allocator (e.g. `reserve_thp`, `utopia`) — it carries the same hash
and prediction knobs the allocator would otherwise supply.

### Key knobs (`[perf_model/revelator]`)

| Knob | Meaning |
|------|---------|
| `number_of_hashes` | How many hash functions the allocator/engine use |
| `number_of_predictions` | Speculative translations issued per lookup |
| `memory_size`, `kernel_size` | Total and kernel-reserved memory, in MB |
| `target_fragmentation` | `1.0` = 0% utilized, `0.0` = 100% utilized |
| `frag_type` | Fragmentation model (`largepages`, …) |
| `oracle`, `perfect_filtering` | Upper-bound / idealized modes for limit studies |
| `filter` | Enable the prediction filter |
| `hash1_usage_threshold` | Occupancy at which the allocator falls back off hash 1 |
| `enable_aggressive_swapouts`, `infrequency_threshold` | Swap-out aggressiveness |

---

## Running

### Single node, one config

```bash
cd simulator/sniper
./run-sniper -c config/address_translation_schemes/revelator.cfg \
             --traces=/path/to/trace.sift -d /path/to/outdir
```

### Multicore suites

`experiments/clist_multicore.yaml` defines ready-made Revelator suites:

| Suite family | Suites |
|--------------|--------|
| 4KB-only, homogeneous/heterogeneous mixes | `revelator_4kb_{2,4,8}core_{homo,hetero}` |
| Utilization sweep (2× memory, `target_fragmentation` sweep) | `revelator_util_sweep_*` |
| Top-250 mixes | `revelator_4kb_{2,4,8,16}core_top250` |

```bash
python3 experiments/create_experiments.py \
  --artifact-path /path/to/Virtuoso \
  --yaml experiments/clist_multicore.yaml \
  --suite revelator_4kb_4core_homo \
  --suite-dir-name exp_revelator_4c \
  --force

python3 experiments/safe_submit.py experiments/exp_revelator_4c/jobfile.sh --dry-run
```

There are no Revelator suites in the single-core `experiments/clist.yaml`; run
single-core configs directly with `run-sniper`, or add a suite following the
pattern in `clist_multicore.yaml`.

### Hardware area and power

See `revelator_auxiliary/RTL/README.md` for the yosys synthesis invocation and
`static_power_analyzer.py` usage. The Chisel sources referenced there are not part
of this branch.
