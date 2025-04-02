---
sidebar_position: 1
---

# Release Notes

## **2025-07: Planned 3rd Major Update**
- **New Features:**
    - Integration of MimicOS with a [CXL simulator](https://github.com/Amit-P89/-DRackSim/)
    - Integration of MimicOS with a GPU simulator.
    - Virtuoso+Sniper with support for IOMMU.
    - MimicOS with network stack support.

---

## **2025-05: Planned 2nd Major Update**
- **New Features:**
    - Release of all MimicOS modules, including:
        - hugetlbfs
        - Page cache
        - Swap cache
          
    - Integration of MimicOS with:
        - [gem5-SE](http://gem5.org/)
        - [Ramulator](https://github.com/CMU-SAFARI/ramulator2)
        - [Sniper](https://github.com/snipersim/)
        - [ChampSim](https://github.com/ChampSim/ChampSim)
          
    - New traces/workloads with address translation overheads:
        - MemCached
        - Redis
        - Stockfish
          
    - Release of memory tagging schemes

---

## **2025-04-02: Initial Release**
- **Virtuoso Integration:**
    - [Sniper Multi-Core Simulator] (https://github.com/snipersim/)

- **MMU Models:**
    1. **MMU Baseline:**
         - Page Walk Caches
         - Configurable TLB hierarchy.
         - Configurable Page Walk Cache (PWC) hierarchy
         - Large page prediction based on [Papadopoulou et al.](https://ieeexplore.ieee.org/document/7056034)
    2. **MMU Speculation:** Speculative address translation as described in [SpecTLB](https://ieeexplore.ieee.org/document/6307767)
    3. **MMU Software-Managed TLB:** Software-managed L3 TLB as described in [POM-TLB](https://ieeexplore.ieee.org/document/8192494)
    4. **MMU Utopia:** Implements [Utopia](https://arxiv.org/abs/2211.12205)
    5. **MMU Midgard:** Implements [Midgard](https://dl.acm.org/doi/10.1109/ISCA52012.2021.00047)
    6. **MMU RMM (and Direct Segments):** Implements [RMM](https://scail.cs.wisc.edu/papers/isca15-rmm.pdf)
    7. **MMU Virtualized:** Nested Paging and Nested Page Tables (NPT) for modern hypervisors

- **Page Table Designs:**
    1. **Page Table Baseline:** Radix page table with configurable page sizes
    2. **Range Table:** B++ Tree-like translation table for [virtual-to-physical address ranges](https://scail.cs.wisc.edu/papers/isca15-rmm.pdf)
    3. **Hash Don't Cache:** [Open-addressing hash-based page table](https://dl.acm.org/doi/10.1145/2964791.2901456)
    4. **Conventional Hash-Based:** Chain-based hash table design
    5. **ECH:** [Cuckoo hashing-based organization of the page table](https://iacoma.cs.uiuc.edu/iacoma-papers/asplos20.pdf)
    6. **RobinHood:** Open-addressing with element re-ordering

- **Memory Allocation Policies:**
    1. **Reservation-Based THP:** Implements reservation-based Transparent Huge Pages
    2. **Eager-Paging:** Implements the contiguity-based allocation described in [RMM](https://scail.cs.wisc.edu/papers/isca15-rmm.pdf)
    3. **Utopia:** Implements the allocation mechanism described in [Utopia](https://arxiv.org/abs/2211.12205)


