#include "nuca_cache.h"
#include "memory_manager_base.h"
#include "pr_l1_cache_block_info.h"
#include "config.hpp"
#include "stats.h"
#include "queue_model.h"
#include "shmem_perf.h"
#include "simulator.h"
#include "mimicos.h"
#include "debug_config.h"
#include <iomanip>

// NUCA access tracing for debugging cache conflicts
// Set to 1 to enable tracing, 0 to disable
#define NUCA_TRACE_ENABLED 0
#define NUCA_TRACE_LIMIT 100000

NucaCache::NucaCache(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, AddressHomeLookup* home_lookup, UInt32 cache_block_size, ParametricDramDirectoryMSI::CacheParameters& parameters)
   : m_core_id(memory_manager->getCore()->getId())
   , m_memory_manager(memory_manager)
   , m_shmem_perf_model(shmem_perf_model)
   , m_home_lookup(home_lookup)
   , m_cache_block_size(cache_block_size)
   , m_data_access_time(parameters.data_access_time)
   , m_tags_access_time(parameters.tags_access_time)
   , m_data_array_bandwidth(8 * Sim()->getCfg()->getFloat("perf_model/nuca/bandwidth"))
   , m_queue_model(NULL)
   , m_reads(0)
   , m_writes(0)
   , m_read_misses(0)
   , m_write_misses(0)
{

   std::cout << "[Memory Hierarchy] Initializing NUCA Slice for core " << m_core_id << std::endl;

   m_cache = new Cache("nuca-cache",
      "perf_model/nuca/cache",
      m_core_id,
      parameters.num_sets,
      parameters.associativity,
      m_cache_block_size,
      parameters.replacement_policy,
      CacheBase::PR_L1_CACHE,
      CacheBase::parseAddressHash(parameters.hash_function),
      NULL, /* FaultinjectionManager */
      home_lookup
   );

   if (Sim()->getCfg()->getBool("perf_model/nuca/queue_model/enabled"))
   {
      String queue_model_type = Sim()->getCfg()->getString("perf_model/nuca/queue_model/type");
      m_queue_model = QueueModel::create("nuca-cache-queue", m_core_id, queue_model_type, m_data_array_bandwidth.getRoundedLatency(8 * m_cache_block_size)); // bytes to bits
   }

   registerStatsMetric("nuca-cache", m_core_id, "reads", &m_reads);
   registerStatsMetric("nuca-cache", m_core_id, "writes", &m_writes);
   registerStatsMetric("nuca-cache", m_core_id, "read-misses", &m_read_misses);
   registerStatsMetric("nuca-cache", m_core_id, "write-misses", &m_write_misses);

#if DEBUG_NUCA_CONTENT >= DEBUG_BASIC
   m_last_log_access_count = 0;
   m_content_log_initialized = false;
#endif

}

NucaCache::~NucaCache()
{
   delete m_cache;
   if (m_queue_model)
      delete m_queue_model;

#if DEBUG_NUCA_CONTENT >= DEBUG_BASIC
   if (m_content_log_initialized && m_content_log.is_open()) {
      m_content_log.close();
   }
#endif
}

boost::tuple<SubsecondTime, HitWhere::where_t>
NucaCache::read(IntPtr address, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool count,bool is_metadata)
{
   //std::cout<<"Nuca Read:"<<is_metadata<<"\n";
   HitWhere::where_t hit_where = HitWhere::MISS;
   perf->updateTime(now);

   PrL1CacheBlockInfo* block_info = (PrL1CacheBlockInfo*)m_cache->peekSingleLine(address);

   SubsecondTime latency = m_tags_access_time.getLatency();
   perf->updateTime(now + latency, ShmemPerf::NUCA_TAGS);

   // NUCA access tracing
   #if NUCA_TRACE_ENABLED
   {
      static FILE* nuca_trace_file = nullptr;
      static UInt64 nuca_trace_count = 0;
      
      if (nuca_trace_count < NUCA_TRACE_LIMIT) {
         if (nuca_trace_file == nullptr) {
            String trace_filename = Sim()->getConfig()->getOutputDirectory() + "/nuca_access_trace.csv";
            nuca_trace_file = fopen(trace_filename.c_str(), "w");
            if (nuca_trace_file) {
               fprintf(nuca_trace_file, "access_num,address,set_index,tag,hit,op\n");
               fflush(nuca_trace_file);
            }
         }
         if (nuca_trace_file) {
            // Calculate set index (assuming 2048 sets, 64B lines)
            UInt32 set_index = (address >> 6) & 0x7FF;  // bits 6-16
            UInt64 tag = address >> 17;  // bits 17+
            int hit = (block_info != nullptr) ? 1 : 0;
            fprintf(nuca_trace_file, "%lu,0x%lx,%u,0x%lx,%d,R\n", 
               nuca_trace_count, address, set_index, tag, hit);
            nuca_trace_count++;
            if (nuca_trace_count % 10000 == 0) {
               fflush(nuca_trace_file);
            }
            if (nuca_trace_count == NUCA_TRACE_LIMIT) {
               fflush(nuca_trace_file);
               fclose(nuca_trace_file);
               nuca_trace_file = nullptr;
            }
         }
      }
   }
   #endif

   
   if (block_info)
   {
      m_cache->accessSingleLine(address, Cache::LOAD, data_buf, m_cache_block_size, now + latency, true);

      latency += accessDataArray(Cache::LOAD, now + latency, perf);
      hit_where = HitWhere::NUCA_CACHE;
   }
   else
   {
      if (count) {
            ++m_read_misses;
            
            // Update MimicOS per-core NUCA miss stats
            MimicOS* mimicos = Sim()->getMimicOS();
            if (mimicos && mimicos->isPerCoreStatsInitialized()) {
               PerCoreStats* stats = mimicos->getPerCoreStatsMutable(m_core_id);
               if (stats) {
                  stats->nuca_misses++;
                  stats->nuca_accesses++;
                  if (is_metadata) {
                     stats->nuca_metadata_misses++;
                  } else {
                     stats->nuca_data_misses++;
                  }
               }
            }
      }
   }

   if (count){
      ++m_reads;
      
      // Update MimicOS per-core NUCA access stats (for hits)
      if (hit_where == HitWhere::NUCA_CACHE) {
         MimicOS* mimicos = Sim()->getMimicOS();
         if (mimicos && mimicos->isPerCoreStatsInitialized()) {
            PerCoreStats* stats = mimicos->getPerCoreStatsMutable(m_core_id);
            if (stats) {
               stats->nuca_accesses++;
            }
         }
      }
      
#if DEBUG_NUCA_CONTENT >= DEBUG_BASIC
      // Periodically log NUCA cache content distribution based on NUCA accesses
      logCacheContentDistribution(m_reads + m_writes);
#endif
   }

   return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
NucaCache::write(IntPtr address, Byte* data_buf, bool& eviction, IntPtr& evict_address, Byte* evict_buf, SubsecondTime now, bool count,bool is_metadata)
{
   HitWhere::where_t hit_where = HitWhere::MISS;

   PrL1CacheBlockInfo* block_info = (PrL1CacheBlockInfo*)m_cache->peekSingleLine(address);
   SubsecondTime latency = m_tags_access_time.getLatency();

   if (block_info)
   {
      block_info->setCState(CacheState::MODIFIED);
      m_cache->accessSingleLine(address, Cache::STORE, data_buf, m_cache_block_size, now + latency, true);
      block_info->increaseReuse();
      latency += accessDataArray(Cache::STORE, now + latency, &m_dummy_shmem_perf);
      hit_where = HitWhere::NUCA_CACHE;
   }
   else
   {
      PrL1CacheBlockInfo evict_block_info;

      m_cache->insertSingleLine(address, data_buf,
         &eviction, &evict_address, &evict_block_info, evict_buf,
         now + latency);

      if (eviction)
      {
         if (evict_block_info.getCState() != CacheState::MODIFIED)
         {
            // Unless data is dirty, don't have caller write it back
            eviction = false;
         }
      }

      if (count) {
            ++m_write_misses;
            
            // Update MimicOS per-core NUCA miss stats
            MimicOS* mimicos = Sim()->getMimicOS();
            if (mimicos && mimicos->isPerCoreStatsInitialized()) {
               PerCoreStats* stats = mimicos->getPerCoreStatsMutable(m_core_id);
               if (stats) {
                  stats->nuca_misses++;
                  stats->nuca_accesses++;
                  if (is_metadata) {
                     stats->nuca_metadata_misses++;
                  } else {
                     stats->nuca_data_misses++;
                  }
               }
            }
      }
   }
   
   if (count){
      ++m_writes;
      
      // Update MimicOS per-core NUCA access stats (for hits)
      if (hit_where == HitWhere::NUCA_CACHE) {
         MimicOS* mimicos = Sim()->getMimicOS();
         if (mimicos && mimicos->isPerCoreStatsInitialized()) {
            PerCoreStats* stats = mimicos->getPerCoreStatsMutable(m_core_id);
            if (stats) {
               stats->nuca_accesses++;
            }
         }
      }
   }

   return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where);
}

SubsecondTime
NucaCache::accessDataArray(Cache::access_t access, SubsecondTime t_start, ShmemPerf *perf)
{
   perf->updateTime(t_start);

   // Compute Queue Delay
   SubsecondTime queue_delay;
   if (m_queue_model)
   {
      SubsecondTime processing_time = m_data_array_bandwidth.getRoundedLatency(8 * m_cache_block_size); // bytes to bits

      queue_delay = processing_time + m_queue_model->computeQueueDelay(t_start, processing_time, m_core_id);

      perf->updateTime(t_start + processing_time, ShmemPerf::NUCA_BUS);
      perf->updateTime(t_start + queue_delay, ShmemPerf::NUCA_QUEUE);
   }
   else
   {
      queue_delay = SubsecondTime::Zero();
   }

   perf->updateTime(t_start + queue_delay + m_data_access_time.getLatency(), ShmemPerf::NUCA_DATA);

   return queue_delay + m_data_access_time.getLatency();
}

void NucaCache::markTranslationMetadata(IntPtr address, CacheBlockInfo::block_type_t blocktype){

   
   IntPtr tag;
   UInt32 set_index;

   m_cache->splitAddress(address, tag, set_index);


   for (UInt32 i=0; i < m_cache->getCacheSet(set_index)->getAssociativity(); i++){
            
      if(m_cache->peekBlock(set_index,i)->getTag() == tag){
         m_cache->peekBlock(set_index,i)->setBlockType(blocktype);
         break;
      }
                 
   }

}



void
NucaCache::measureStats()
{
   m_cache->measureStats();
}

void
NucaCache::logCacheContentDistribution(UInt64 nuca_accesses)
{
#if DEBUG_NUCA_CONTENT >= DEBUG_BASIC
   // Only log at specified intervals
   if (nuca_accesses < m_last_log_access_count + NUCA_CONTENT_LOG_INTERVAL) {
      return;
   }
   m_last_log_access_count = nuca_accesses;

   // Initialize log file on first call
   if (!m_content_log_initialized) {
      std::string log_filename = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/nuca_content_core" + std::to_string(m_core_id) + ".csv";
      m_content_log.open(log_filename.c_str());
      if (m_content_log.is_open()) {
         m_content_log << "nuca_accesses,total_blocks,valid_blocks,data_blocks,instruction_blocks,"
                       << "pt_data_blocks,pt_instr_blocks,utopia_fp_blocks,utopia_tar_blocks,"
                       << "invalid_blocks,data_pct,metadata_pct" << std::endl;
         m_content_log_initialized = true;
         std::cout << "[NUCA Content] Opened log file: " << log_filename << std::endl;
      } else {
         std::cerr << "[NUCA Content] Failed to open log file: " << log_filename << std::endl;
         return;
      }
   }

   // Count blocks by type (fine-grained)
   UInt64 total_blocks = 0;
   UInt64 valid_blocks = 0;
   UInt64 data_blocks = 0;
   UInt64 instruction_blocks = 0;
   UInt64 pt_data_blocks = 0;      // Page table for data translations
   UInt64 pt_instr_blocks = 0;     // Page table for instruction translations
   UInt64 utopia_fp_blocks = 0;    // Utopia FPA metadata
   UInt64 utopia_tar_blocks = 0;   // Utopia TAR metadata
   UInt64 invalid_blocks = 0;

   UInt32 num_sets = m_cache->getNumSets();
   UInt32 associativity = m_cache->getAssociativity();

   for (UInt32 set_idx = 0; set_idx < num_sets; ++set_idx) {
      CacheSet* cache_set = m_cache->getCacheSet(set_idx);
      for (UInt32 way = 0; way < associativity; ++way) {
         CacheBlockInfo* block_info = cache_set->peekBlock(way);
         total_blocks++;

         if (block_info->isValid()) {
            valid_blocks++;
            CacheBlockInfo::block_type_t block_type = block_info->getBlockType();
            switch (block_type) {
               case CacheBlockInfo::block_type_t::DATA:
                  data_blocks++;
                  break;
               case CacheBlockInfo::block_type_t::INSTRUCTION:
                  instruction_blocks++;
                  break;
               case CacheBlockInfo::block_type_t::PAGE_TABLE_DATA:
                  pt_data_blocks++;
                  break;
               case CacheBlockInfo::block_type_t::PAGE_TABLE_INSTRUCTION:
                  pt_instr_blocks++;
                  break;
               case CacheBlockInfo::block_type_t::UTOPIA_FP:
                  utopia_fp_blocks++;
                  break;
               case CacheBlockInfo::block_type_t::UTOPIA_TAR:
                  utopia_tar_blocks++;
                  break;
               default:
                  // Treat unknown types as data
                  data_blocks++;
                  break;
            }
         } else {
            invalid_blocks++;
         }
      }
   }

   // Calculate percentages (of valid blocks)
   UInt64 total_metadata = pt_data_blocks + pt_instr_blocks + utopia_fp_blocks + utopia_tar_blocks;
   double data_pct = (valid_blocks > 0) ? (100.0 * (data_blocks + instruction_blocks) / valid_blocks) : 0.0;
   double metadata_pct = (valid_blocks > 0) ? (100.0 * total_metadata / valid_blocks) : 0.0;

   // Write to log
   m_content_log << nuca_accesses << ","
                 << total_blocks << ","
                 << valid_blocks << ","
                 << data_blocks << ","
                 << instruction_blocks << ","
                 << pt_data_blocks << ","
                 << pt_instr_blocks << ","
                 << utopia_fp_blocks << ","
                 << utopia_tar_blocks << ","
                 << invalid_blocks << ","
                 << std::fixed << std::setprecision(2) << data_pct << ","
                 << std::fixed << std::setprecision(2) << metadata_pct << std::endl;

#if DEBUG_NUCA_CONTENT >= DEBUG_DETAILED
   std::cout << "[NUCA Content] Core " << m_core_id << " @ " << nuca_accesses << " accesses: "
             << valid_blocks << "/" << total_blocks << " valid, "
             << data_blocks << " data, " << instruction_blocks << " instr, "
             << "PT[D:" << pt_data_blocks << " I:" << pt_instr_blocks << "] "
             << "Utopia[FP:" << utopia_fp_blocks << " TAR:" << utopia_tar_blocks << "] ("
             << std::fixed << std::setprecision(1) << metadata_pct << "% metadata)" << std::endl;
#endif

#endif // DEBUG_NUCA_CONTENT
}
