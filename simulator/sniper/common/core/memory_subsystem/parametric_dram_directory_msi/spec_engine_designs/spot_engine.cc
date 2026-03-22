#include "spot_engine.h"
#include "simulator.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "cache_block_info.h"
#include "stats.h"
#include "mimicos.h"
#include "thread.h"



static void debug_ways(std::ofstream& log_file, ParametricDramDirectoryMSI::Ways &ways, int set_index) {
    log_file << "[Spot] START DEBUG SET #" << set_index << '\n';
    for (auto& e: ways) {
        log_file << "  Entry: " << std::hex << e.tag << " Offset: " << e.offset << " Valid: " << e.valid << " Saturation: " << e.saturation_counter << " LRU: " << e.LRU_counter << '\n';
    }
    log_file << "[Spot] END DEBUG SET #" << set_index << '\n';
}

/* END DEBUG FUNCTIONS */

static bool offset_match(UInt64 va, UInt64 pa, int64_t offset) { 
    return static_cast<int64_t>(va) - static_cast<int64_t>(pa) == static_cast<int64_t>(offset << 12);
}

namespace ParametricDramDirectoryMSI
{
        SpotEntry EMPTY_SPOT_ENTRY = { // Definition of extern declaration from spot_engine.h
            .valid = false,
            .tag = 0,
            .offset = 0,
            .saturation_counter = 1,
            .LRU_counter = 0
        };

        Spot::Spot(Core *core, MemoryManagerBase *_memory_manager, ShmemPerfModel *shmem_perf_model, String _name) : 
            SpecEngineBase(core, _memory_manager, shmem_perf_model, _name), 
            memory_manager(_memory_manager), 
            name(_name),
            predefined_allocation_threshold(Sim()->getCfg()->getInt("perf_model/spot_allocator/predefined_allocation_threshold")),
            m_last_spec_completion(SubsecondTime::Zero()),
            m_last_prediction_correct(false)
        {

            log_file_name = std::string(name.c_str()) + ".spec_engine.log";
            log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
            log_file.open(log_file_name);


            // Getting config values
            number_of_entries_bits = Sim()->getCfg()->getInt("perf_model/spot_allocator/number_of_entries_bits");           
            number_of_entries      = (1 << number_of_entries_bits);           
            number_of_ways         = Sim()->getCfg()->getInt("perf_model/spot_allocator/number_of_ways");                           
            number_of_sets         = Sim()->getCfg()->getInt("perf_model/spot_allocator/number_of_sets");                           ;
            number_of_sets_bits    = static_cast<int>(log2(number_of_sets));

            log_file << "[Spot] number_of_entries_bits = " << number_of_entries_bits  << std::endl  
                    << "[Spot] number_of_entries = " << number_of_entries << std::endl
                    << "[Spot] number_of_ways = " << number_of_ways << std::endl
                    << "[Spot] number_of_sets = " << number_of_sets << std::endl
                    << "[Spot] number_of_sets_bits = " << number_of_sets_bits << std::endl;

            cache.assign(number_of_sets, Ways(number_of_ways, EMPTY_SPOT_ENTRY));

    #ifdef DEBUG_SPOT
            std::cout << "[Spot] Initializing Spot" << std::endl;
            std::cout << "[Spot] Number of entries: " << (1 << number_of_entries_bits) << std::endl;
            std::cout << "[Spot] Number of ways: "    <<  number_of_ways << std::endl;
            std::cout << "[Spot] Number of sets: "    <<  number_of_sets << std::endl;
    #endif


            bzero(&stats, sizeof(stats));
            
            registerStatsMetric("spot", core->getId(), "cache_accesses", &stats.cache_accesses);
            registerStatsMetric("spot", core->getId(), "cache_hits", &stats.cache_hits);
            registerStatsMetric("spot", core->getId(), "cache_misses", &stats.cache_misses);

            registerStatsMetric("spot", core->getId(), "predictions_made", &stats.predictions_made);
            registerStatsMetric("spot", core->getId(), "prediction_not_made_saturation", &stats.prediction_not_made_saturation);
            registerStatsMetric("spot", core->getId(), "predictions_correct", &stats.predictions_correct);
            registerStatsMetric("spot", core->getId(), "prediction_accuracy", &stats.prediction_accuracy);
            registerStatsMetric("spot", core->getId(), "allocations", &stats.allocations);
            registerStatsMetric("spot", core->getId(), "no_allocation_confidence", &stats.no_allocation_confidence);
            registerStatsMetric("spot", core->getId(), "no_allocation_offset_mismatch", &stats.no_allocation_offset_mismatch);

            registerStatsMetric("spot", core->getId(), "evictions_zero_confidence", &stats.evictions_zero_confidence);
            registerStatsMetric("spot", core->getId(), "allocations_bypassed_confident_set", &stats.allocations_bypassed_confident_set);
            registerStatsMetric("spot", core->getId(), "allocation_in_invalid_way", &stats.allocation_in_invalid_way);
            registerStatsMetric("spot", core->getId(), "refreshed_hit_entry", &stats.refreshed_hit_entry);

        }

        void Spot::invokeSpecEngine(IntPtr address, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled, SubsecondTime invoke_start_time, IntPtr physical_address, bool page_table_speculation)
        {
            // Reset per-invocation tracking
            m_last_spec_completion = SubsecondTime::Zero();
            m_last_prediction_correct = false;

            if(!page_table_speculation) {
    #ifdef DEBUG_SPOT
                log_file << "[Spot] Invoking Spot non-speculatively(page_table_speculation = false) with address " << address << " and eip " << eip <<" page_table_spec "<<page_table_speculation<< std::endl;
    #endif
                UInt64 PC       = eip;
                UInt64 index    = PC % number_of_sets;
                UInt64 tag      = PC >> number_of_sets_bits;
                int64_t vpn     = (address >> 12);
                Ways&   ways    = cache[index]; // reference...

                // Count 1 access to a set as one cache access, not as 'num_ways' accesses
                stats.cache_accesses++;
                bool cache_hit = false; 
                for (auto& entry: ways) {
                    if(tag==entry.tag && entry.valid) {
                        entry.LRU_counter = (++global_LRU_counter); // @vlnitu: Update LRU counter
                        stats.cache_hits++;

        #ifdef DEBUG_SPOT
                            log_file << "[Spot] Cache Hit - VA = " << std::hex << address << " - PA = " << std::hex << physical_address << " - Offset = " << std::hex << entry.offset << std::endl;
        #endif


                        if(entry.saturation_counter > 1) { // Enough confidence to make prediction 
                            IntPtr predicted_pa = ((IntPtr)(vpn - entry.offset) << 12) | (address & 0xFFF);
                            auto* l2_cntlr = memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L2_CACHE);
                            l2_cntlr->handleMMUPrefetch(eip, predicted_pa, invoke_start_time);
                            m_last_spec_completion = l2_cntlr->getLastPrefetchCompletion();
        #ifdef DEBUG_SPOT
                            log_file << "[Spot] Prefetching address " << std::hex << predicted_pa << std::endl;
        #endif
                            stats.predictions_made++;
                            if (offset_match(address, physical_address, entry.offset)) {
                                stats.predictions_correct++;
                                m_last_prediction_correct = true;
        #ifdef DEBUG_SPOT
                            log_file << "[Spot] Speculation CORRECT - VA = " << std::hex << address << " - PA = " << std::hex << physical_address << " - Offset = " << std::hex << entry.offset << std::endl;
        #endif
                                if (entry.saturation_counter < 3) {
                                    entry.saturation_counter++;
                                }
                            }
                            else {
                                m_last_prediction_correct = false;
                                // No pipeline flush penalty: we only prefetch, we don't
                                // feed the predicted PA into the execution pipeline.
        #ifdef DEBUG_SPOT
                            log_file << "[Spot] Speculation INCORRECT - VA = " << std::hex << address << " - PA = " << std::hex << physical_address << " - Offset = " << std::hex << entry.offset << std::endl;
        #endif
                                if(entry.saturation_counter > 0){
                                    entry.saturation_counter--;
                                }
                            }
        #ifdef DEBUG_SPOT
                            log_file << "[Spot] Prediction made for address " << std::hex << address << " with physical address " << physical_address << " and offset " << entry.offset << std::dec << std::endl;
                            log_file<< "[Spot] Prediction accuracy: " << (float)stats.predictions_correct  << " "<<(float)stats.predictions_made << std::endl;
        #endif
                            stats.prediction_accuracy =
                                (UInt64)(100.0 * (double)stats.predictions_correct / (double)stats.predictions_made);
                        } 
                        else{
                            stats.prediction_not_made_saturation++;
        #ifdef DEBUG_SPOT
                            log_file << "[Spot] Prediction not made due to saturation counter " << entry.saturation_counter << std::endl;
        #endif
                        }

                        // Found matching entry
                        cache_hit = true;
                        break;
                    }
                }

                if (!cache_hit) {
    #ifdef DEBUG_SPOT
                    log_file << "[Spot] Cache Miss - VA = " << std::hex << address << " - PA = " << std::hex << physical_address << " - Tag = " << std::hex << tag << std::endl;
                    debug_ways(log_file, ways, index);
    #endif
                    stats.cache_misses++;
                }
            }
            else {
    #ifdef DEBUG_SPOT
                log_file << "[Spot] Invoking Spot speculatively(page_table_speculation = true) - nop (doesn't have any effect) - with address " << address << " and eip " << eip <<" page_table_spec "<<page_table_speculation<< std::endl;
    #endif
            }
        }

    void Spot::allocateInSpecEngine(IntPtr virt_addr, IntPtr ppn, int count,
                                    Core::lock_signal_t lock, IntPtr eip, bool modeled)
    {
        UInt64 PC    = eip;
        UInt64 index = PC % number_of_sets;
        UInt64 tag   = PC >> number_of_sets_bits;
        UInt64 vpn   = (virt_addr >> 12);

        int app_id = core->getThread()->getAppId();

        // Load the offset & confidence gate (OS/allocator side)
        IntPtr offset            = Sim()->getMimicOS()->getPhysicalOffsetSpot(app_id, virt_addr);
        int successful_allocs    = Sim()->getMimicOS()->getSuccessfulOffsetBasedAllocations(app_id, virt_addr);

        // Only learn if the VA->PA delta matches AND we have enough contiguity evidence.
        if (!(offset_match(virt_addr, ppn << 12, offset))) {
            stats.no_allocation_offset_mismatch++;
            return;
        }
        
        if (!(successful_allocs > predefined_allocation_threshold)) {
            stats.no_allocation_confidence++;
            return;
        }

        Ways& ways = cache[index];

        // (Optional) If an entry with the same tag exists, refresh it rather than duplicating.
        for (auto &entry : ways) {
            if (entry.valid && entry.tag == tag) {
                entry.offset = offset;                           // keep/refresh learned offset
                entry.LRU_counter = (++global_LRU_counter);      // touch
                if (entry.saturation_counter < 3)                // 2-bit max = 3
                    entry.saturation_counter++;
                // You can count this as an allocation or a refresh; I use allocations.
                stats.allocations++;
                stats.refreshed_hit_entry++;
                return;
            }
        }

        // 1) Try to place in an invalid way
        for (int i = 0; i < number_of_ways; ++i) {
            if (!ways[i].valid) {
                ways[i] = (SpotEntry){
                    .valid              = true,
                    .tag                = tag,
                    .offset             = offset,
                    .saturation_counter = 1,                     // init = 1
                    .LRU_counter        = (++global_LRU_counter)
                };
                stats.allocations++;
                stats.allocation_in_invalid_way++;
                return;
            }
        }

        // 2) Set is full: prefer evicting the LRU among entries with counter == 0
        int victim = -1;
        UInt64 best_lru = UINT64_MAX;
        for (int i = 0; i < number_of_ways; ++i) {
            if (ways[i].saturation_counter == 0) {
                if (ways[i].LRU_counter < best_lru) {
                    best_lru = ways[i].LRU_counter;
                    victim = i;
                }
            }
        }

        if (victim != -1) {
            ways[victim] = (SpotEntry){
                .valid              = true,
                .tag                = tag,
                .offset             = offset,
                .saturation_counter = 1,                         // init = 1
                .LRU_counter        = (++global_LRU_counter)
            };
            stats.allocations++;
            stats.evictions_zero_confidence++;
            return;
        }

        // 3) All entries have counter > 0 (they’re “confident”) -> BYPASS (don't evict): p. 521 paper (p. 7)
        // Don’t overwrite a hot/confident entry; wait until someone’s counter drops to 0.
        stats.allocations_bypassed_confident_set++;
        return;
    }

};