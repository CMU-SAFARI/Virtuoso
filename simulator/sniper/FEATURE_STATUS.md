# Sniper Simulator Feature Status

> Last Updated: December 16, 2025

This document tracks the status of simulator features, configurations, and components.

---

## Table of Contents
- [Core Configurations](#core-configurations)
- [Address Translation Schemes](#address-translation-schemes)
- [Physical Memory Allocators](#physical-memory-allocators)
- [Page Table Implementations](#page-table-implementations)
- [MMU Designs](#mmu-designs)
- [Speculative Engines](#speculative-engines)
- [Prefetchers](#prefetchers)
- [Testing Framework](#testing-framework)
- [Known Issues](#known-issues)

---

## Core Configurations

| Config | Status | Description |
|--------|--------|-------------|
| `beefy.cfg` | ✅ Working | Larger caches (64KB L1, 1MB L2), 400-entry ROB |
| `meteor_lake_pcore.cfg` | ✅ Working | Modern P-core: 5.1 GHz, 512-entry ROB, 2MB L2 |
| `wimpy.cfg` | ❓ Unknown | Small core configuration |
| `biglittle.cfg` | ❓ Unknown | Heterogeneous big.LITTLE config |

---

## Address Translation Schemes

### Basic Schemes
| Config | Status | Description |
|--------|--------|-------------|
| `baseline.cfg` | ✅ Working | 4-level radix + baseline allocator |
| `reservethp.cfg` | ✅ Working | Radix + ReserveTHP allocator |
| `asap.cfg` | 🔶 Untested | ASAP allocator with radix |
| `dmt.cfg` | 🔶 Untested | Direct Memory Translation MMU |
| `spot.cfg` | 🔶 Untested | SPOT prefetcher + contiguity allocator |

### Page Table Variants
| Config | Status | Description |
|--------|--------|-------------|
| `ech_baseline_alloc.cfg` | 🔶 Untested | Elastic Cuckoo Hash + baseline |
| `hdc_baseline_alloc.cfg` | 🔶 Untested | Hash Don't Cache + baseline |
| `ht_baseline_alloc.cfg` | 🔶 Untested | Hash Table chaining + baseline |
| `radix_utopia.cfg` | 🔶 Untested | Radix + Utopia allocator |

### ReserveTHP Variants
| Config | Status | Description |
|--------|--------|-------------|
| `reservethp_ech.cfg` | 🔶 Untested | ReserveTHP + ECH page table |
| `reservethp_hdc.cfg` | 🔶 Untested | ReserveTHP + HDC page table |
| `reservethp_ht.cfg` | 🔶 Untested | ReserveTHP + HT chaining |
| `reservethp_pomtlb.cfg` | 🔶 Untested | ReserveTHP + POM-TLB MMU |

### Speculative Schemes
| Config | Status | Description |
|--------|--------|-------------|
| `spec_oracle_reservethp.cfg` | 🔶 Untested | Oracle speculation + ReserveTHP |
| `spectlb_reservethp.cfg` | 🔶 Untested | SpecTLB + ReserveTHP |
| `rmm.cfg` | 🔶 Untested | Redundant Memory Mappings |

---

## Physical Memory Allocators

| Allocator | Status | Location |
|-----------|--------|----------|
| `baseline_allocator` | ✅ Working | `physical_memory_allocators/baseline_allocator.cfg` |
| `reserve_thp` | ✅ Working | `physical_memory_allocators/reserve_thp.cfg` |
| `utopia` | 🔶 Untested | `physical_memory_allocators/utopia.cfg` |
| `eager_paging` | 🔶 Untested | `physical_memory_allocators/eager_paging.cfg` |
| `spot` | 🔶 Untested | `physical_memory_allocators/spot.cfg` |
| `swap_space` | ❓ Unknown | `physical_memory_allocators/swap_space.cfg` |

### Utopia Allocator (New Implementation)
| Component | Status | Notes |
|-----------|--------|-------|
| Template-based allocator | ✅ Built | `include/memory_management/physical_memory_allocators/utopia.h` |
| RestSeg cache structure | ✅ Built | Custom LRU-based implementation |
| Sniper policy integration | ✅ Built | `policies/utopia_policy.h` |
| Buddy allocator traits | ✅ Built | `policies/buddy_policy.h` |

---

## Page Table Implementations

| Page Table | Status | Config Location |
|------------|--------|-----------------|
| 4-level radix | ✅ Working | `pagetable_configs/4_level_radix.cfg` |
| 4-level radix (virt) | 🔶 Untested | `pagetable_configs/4_level_radix_virt.cfg` |
| Elastic Cuckoo Hash | 🔶 Untested | `pagetable_configs/elastic_cuckoo_hash_table_asplos2020.cfg` |
| Hash Don't Cache | 🔶 Untested | `pagetable_configs/hash_dont_cache_sigmetrics16.cfg` |
| Hash Table Chaining | 🔶 Untested | `pagetable_configs/hash_table_chaining.cfg` |
| Range Table | ✅ Working | `pagetable_configs/range_table.cfg` |

---

## MMU Designs

| MMU | Status | Config Location |
|-----|--------|-----------------|
| `mmu_base` | ✅ Working | `mmu_configs/mmu_base.cfg` |
| `mmu_spec` | 🔶 Untested | `mmu_configs/mmu_spec.cfg` |
| `mmu_dmt` | 🔶 Untested | `mmu_configs/mmu_dmt.cfg` |
| `mmu_rmm` | 🔶 Untested | `mmu_configs/mmu_rmm.cfg` |
| `mmu_pomtlb` | 🔶 Untested | `mmu_configs/mmu_pomtlb.cfg` |
| `mmu_utopia` | 🔶 Untested | `mmu_configs/mmu_utopia.cfg` |
| `mmu_virt` | 🔶 Untested | `mmu_configs/mmu_virt.cfg` |

---

## Speculative Engines

| Engine | Status | Config Location |
|--------|--------|-----------------|
| `spec_engine_oracle` | 🔶 Untested | `spec_engine_configs/spec_engine_oracle.cfg` |
| `spec_engine_spectlb` | 🔶 Untested | `spec_engine_configs/spec_engine_spectlb.cfg` |
| `spec_engine_spot` | 🔶 Untested | `spec_engine_configs/spec_engine_spot.cfg` |

---

## Prefetchers

| Prefetcher | Status | Type |
|------------|--------|------|
| `simple` | ✅ Working | Next-line prefetcher |
| `ip_stride` | ✅ Working | PC-indexed stride |
| `ghb` | ✅ Working | Global History Buffer |
| `streamer` | ✅ Working | Stream prefetcher |

### Prefetcher Configurations
| Config | Status | Description |
|--------|--------|-------------|
| `no_prefetch.cfg` | ✅ Ready | Disables all prefetching |
| `ip_stride_l1.cfg` | 🔶 Untested | IP-stride at L1D only |
| `streamer_l2.cfg` | 🔶 Untested | Streamer at L2 only |
| `ghb_l1.cfg` | 🔶 Untested | GHB at L1D |
| `aggressive_multilevel.cfg` | 🔶 Untested | IP-stride L1 + Streamer L2 |
| `meteor_lake_prefetch.cfg` | 🔶 Untested | Modern Intel-style |
| `conservative.cfg` | 🔶 Untested | Low-aggression |

---

## Testing Framework

| Component | Status | Location |
|-----------|--------|----------|
| Test runner script | ✅ Working | `testing/run_tests.py` |
| YAML configuration | ✅ Working | `testing/test_config.yaml` |
| Parallel execution | ✅ Working | `--parallel N` flag |
| Suite execution | ✅ Working | `--suite <name>` flag |
| Extends feature | ✅ Working | `-g` parameter overrides |

---

## Known Issues

### Build Issues
- [ ] *None currently tracked*

### Runtime Issues
- [ ] *None currently tracked*

### Configuration Issues
- [ ] *None currently tracked*

---

## Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Working - Tested and functional |
| 🔶 | Untested - Built but needs testing |
| ❌ | Broken - Known issues |
| ❓ | Unknown - Status not verified |
| 🚧 | In Progress - Currently being developed |

---

## Testing History

| Date | Test | Result | Notes |
|------|------|--------|-------|
| *Add entries as tests are run* | | | |

---

## Configuration Structure

```
config/
├── address_translation_schemes/   # Complete scheme configs
├── common_configs/                # Shared base settings
│   └── base_system.cfg           # Simulation basics only
├── core_configs/                  # Core + cache configs
│   ├── beefy.cfg
│   └── meteor_lake_pcore.cfg
├── dram_configs/                  # DRAM timing configs
├── mmu_configs/                   # MMU design configs
├── pagetable_configs/             # Page table configs
├── physical_memory_allocators/    # Allocator configs
├── prefetcher_configs/            # Prefetcher configs
└── spec_engine_configs/           # Speculation engine configs
```

---

## Notes

- All address_translation_schemes configs now use modular includes
- base_system.cfg contains only simulation basics (total_cores, etc.)
- Core/cache settings are in core_configs/
- Old verbose configs preserved in `address_translation_schemes/old_configs/`
