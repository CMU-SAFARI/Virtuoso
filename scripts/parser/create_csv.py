import os
import csv
import sys

stats = {
    "Trace": None,
    "Exp": None,
    "Config": None,
    "performance_model.cycle_count": None,
    "performance_model.elapsed_time_end": None,
    "performance_model.instruction_count": None,
    "ddr.page-closing": None,
    "ddr.page-conflict-data-to-data": None,
    "ddr.page-conflict-data-to-metadata": None,
    "ddr.page-conflict-metadata-to-data": None,
    "ddr.page-conflict-metadata-to-metadata": None,
    "ddr.page-empty": None,
    "ddr.page-hits": None,
    "ddr.page-miss": None,
    "ddr.reads": None,
    "ddr.writes": None,
    "L2.load-misses-non_page_table": None,
    "L2.load-misses-page_table": None,
    "L2.tload-misses": None,
    "L2.tloads": None,
    "L2.prefetches": None,
    "L2.prefetches-fillup": None,
    "L2.hits-prefetch": None,
    "L2.evict-prefetch": None,
    "L1-D.load-misses-non_page_table": None,
    "L1-D.load-misses-page_table": None,
    "L1-D.tload-misses": None,
    "L1-D.tloads": None,
    "L1-D.loads-where-nuca-cache": None,
    "L1-D.loads-where-dram-local": None,
    "L1-D.loads-where-L2": None,
    "L1-D.loads-where-L1": None,
    "L1-D.loads-where-page-table-dram-local": None,
    "L1-D.loads-where-page-table-L2": None,
    "L1-D.loads-where-page-table-nuca-cache": None,
    "L1-D.loads-where-page-table-L1": None,
    "L1-D.total-data-latency": None,
    "pwc_L2.access": None,
    "pwc_L2.miss": None,
    "pwc_L3.access": None,
    "pwc_L3.miss": None,
    "pwc_L4.access": None,
    "pwc_L4.miss": None,
    "radix_4level.allocated_frames": None,
    "radix_4level.page_faults": None,
    "radix_4level.page_size_discovery_0": None,
    "radix_4level.page_size_discovery_1": None,
    "radix_4level.page_table_walks": None,
    "radix_4level.pf_num_cache_accesses": None,
    "radix_4level.ptw_num_cache_accesses": None,
    "mmu_TLB_L1_1.access": None,
    "mmu_TLB_L1_1.eviction": None,
    "mmu_TLB_L1_1.misses": None, 
    "mmu_TLB_L1_2.access": None,
    "mmu_TLB_L1_2.eviction": None,
    "mmu_TLB_L1_2.misses": None,
    "mmu_TLB_L1_3.access": None,
    "mmu_TLB_L1_3.eviction": None,
    "mmu_TLB_L1_3.misses": None,
    "mmu_TLB_L2_1.access": None,
    "mmu_TLB_L2_1.eviction": None,
    "mmu_TLB_L2_1.misses": None,
    "mmu.page_faults": None,
    "mmu.tlb_latency_0": None,
    "mmu.tlb_latency_1": None,
    "mmu.total_table_walk_latency": None,
    "mmu.total_fault_latency": None,
    "mmu.total_tlb_latency": None,
    "mmu.total_translation_latency": None,
    "mmu.frontend_latency": None,
    "mmu.backend_latency": None,
    "mmu.total_rsw_latency": None,
    "mmu.vmas": None,
    "mmu.ptw_level_0": None,
    "mmu.ptw_level_1": None,
    "mmu.ptw_level_2": None,
    "mmu.ptw_level_3": None,
    "pt_elastic_cuckoo.hit_at_level0": None,
    "pt_elastic_cuckoo.hit_at_level1": None,
    "pt_elastic_cuckoo.hits": None,
    "pt_elastic_cuckoo.num_accesses0": None,
    "pt_elastic_cuckoo.num_accesses1": None,
    "pt_elastic_cuckoo.num_fault_accesses0": None,
    "pt_elastic_cuckoo.num_fault_accesses1": None,
    "pt_elastic_cuckoo.pagefaults": None,
    "pt_hash_hdc.accesses_12": None,
    "pt_hash_hdc.accesses_21": None,
    "pt_hash_hdc.collisions_12": None,
    "pt_hash_hdc.collisions_21": None,
    "pt_hash_hdc.page_fault": None,
    "pt_hash_hdc.page_table_walks_12": None,
    "pt_hash_hdc.page_table_walks_21": None,
    "pt_hash.accesses_12": None,
    "pt_hash.accesses_21": None,
    "pt_hash.conflicts_12": None,
    "pt_hash.conflicts_21": None,
    "pt_hash.latency_12": None,
    "pt_hash.latency_21": None,
    "pt_hash.page_fault": None,
    "pt_hash.page_table_walks_12": None,
    "pt_hash.page_table_walks_21": None,
    "asp_tlb.failed_prefetches": None,
    "asp_tlb.prefetch_attempts": None,
    "asp_tlb.successful_prefetches": None,
    "PQ_1.access": None,
    "PQ_1.eviction": None,
    "PQ_1.miss": None,
    "reserve_thp_allocator.four_kb_allocated": None,
    "reserve_thp_allocator.kernel_pages_used": None,
    "reserve_thp_allocator.total_allocations": None,
    "reserve_thp_allocator.two_mb_demoted": None,
    "reserve_thp_allocator.two_mb_promoted": None,
    "reserve_thp_allocator.two_mb_reserved": None,

}




# Create a CSV with all these headers
with open(sys.argv[2], mode='w') as csvfile:

    # Write the headers
    writer = csv.DictWriter(csvfile, fieldnames=stats.keys())
    writer.writeheader()

    path = sys.argv[1]

    for experiment in os.listdir(path):
        if (os.path.isdir(path + experiment) == True):
            row = {}
        
            trace = experiment.split("_")[1]
            config = experiment.split("_")[0]
            row["Trace"] = trace
            row["Config"] = config
            # Check if the sim.stats file exists
            if (os.path.exists(path+experiment+"/sim.stats") == False):
                print("No sim.stats file found for experiment: " + experiment)
                continue

            with open(path + experiment + "/sim.stats") as f:

                lines = f.readlines()
                for line in lines:
                    key = line.split("=")[0]
                    value = line.split("=")[1]
                    key = key.replace(" ", "")
                    key.strip("\n")

                    if key in stats:
                        row[key] = float(value)
            writer.writerow(row)