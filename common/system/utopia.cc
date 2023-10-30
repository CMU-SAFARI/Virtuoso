#include "pwc.h"
#include "stats.h"
#include "config.hpp"
#include "simulator.h"
#include <cmath>
#include <iostream>
#include <utility>
#include "core_manager.h"
#include "cache_set.h"
#include "utopia.h"
#include "utils.h"
#include "hash_map_set.h"

RestSeg::RestSeg(int _id, int _size, int _page_size, int _assoc, String _repl, String _hash) : id(_id),
                                                                                               size(_size),
                                                                                               page_size(_page_size),
                                                                                               assoc(_assoc),
                                                                                               hash(_hash),
                                                                                               repl(_repl),
                                                                                               m_RestSeg_conflicts(0),
                                                                                               m_RestSeg_accesses(0),
                                                                                               m_RestSeg_hits(0)
{

  RestSeg_lock = new Lock();

  num_sets = k_MEGA * size / (assoc * (1 << page_size)); // Number of sets in the RestSeg

  std::cout << std::endl;
  std::cout << "[UTOPIA] Creating RestSeg with sets : " << num_sets << " - page_size: " << page_size << " - assoc: " << assoc << std::endl;

  // We use the same cache structure as the data caches but we specify the "cache_block_size" to be the page size
  m_RestSeg_cache = new Cache(("RestSeg_cache_" + std::to_string(id)).c_str(), "perf_model/utopia/RestSeg", 0, num_sets, assoc, (1L << page_size), repl, CacheBase::PR_L1_CACHE, CacheBase::parseAddressHash(hash));

  filter_size = log2(assoc) + 1;

  tag_size = (48 - page_size - ceil(log2(num_sets))) / 8;

  TAR_size = tag_size * (num_sets * assoc) / 8;

  SF_size = (num_sets * filter_size) / 8;

  int core_num;
  core_num = Config::getSingleton()->getTotalCores();

  for (int i = 0; i < core_num; i++)
  {

    // Get an address from the mem allocator

    std::cout << "[MMU:RestSeg:CoreID-" << i << "] "
              << "SF Size (KB): " << SF_size / 1024 << std::endl;

    std::cout
        << "[MMU:RestSeg:CoreID-"
        << i
        << "]"
        << " TAR entry size: "
        << tag_size
        << std::endl;

    std::cout << "[MMU:RestSeg:CoreID-" << i << "] "
              << "TAR Size (KB): " << TAR_size / 1024 << std::endl;

    int core_num = Config::getSingleton()->getTotalCores();

    char *RestSeg_tags_base = (char *)malloc(num_sets * (tag_size / 8));   // We store a RestSeg TAR per core (as if each core is a process)
    char *RestSeg_per_base = (char *)malloc((num_sets * log2(assoc)) / 8); // We store a RestSeg SF per core (as if each core is a process)

    permissions.push_back((IntPtr)RestSeg_per_base); // Store the address of the RestSeg SF in the permissions vector
    tags.push_back((IntPtr)RestSeg_tags_base);       // Store the address of the RestSeg TAR in the tags vector
  }

  registerStatsMetric(("RestSeg_" + std::to_string(id)).c_str(), 0, "allocation_conflicts", &m_RestSeg_conflicts);
  registerStatsMetric(("RestSeg_" + std::to_string(id)).c_str(), 0, "hits", &m_RestSeg_hits);
  registerStatsMetric(("RestSeg_" + std::to_string(id)).c_str(), 0, "accesses", &m_RestSeg_accesses);
  registerStatsMetric(("RestSeg_" + std::to_string(id)).c_str(), 0, "allocations", &m_allocations);
  registerStatsMetric(("RestSeg_" + std::to_string(id)).c_str(), 0, "RestSeg_utilization", &m_RestSeg_conflicts, true);
}

bool RestSeg::inRestSeg(IntPtr address, bool count, SubsecondTime now, int core_id)
{

  RestSeg_lock->acquire();

  m_RestSeg_accesses++;
  bool hit = m_RestSeg_cache->accessSingleLine(address, Cache::LOAD, NULL, 0, now, true);

  IntPtr tag;
  UInt32 set_index;
  m_RestSeg_cache->splitAddress(address, tag, set_index);

  bool owner_hit = false;
  for (int i = 0; i < m_RestSeg_cache->getCacheSet(set_index)->getAssociativity(); i++)
  {

    if ((m_RestSeg_cache->peekBlock(set_index, i)->getTag() == tag) && (m_RestSeg_cache->peekBlock(set_index, i)->getOwner() == (core_id + 1)))
    {
      m_RestSeg_cache->peekBlock(set_index, i)->increaseReuse();
      owner_hit = true;
      break;
    }
  }

  if (count && hit && owner_hit)
    m_RestSeg_hits++;

  RestSeg_lock->release();

  return (owner_hit);
}

bool RestSeg::inRestSegnostats(IntPtr address, bool count, SubsecondTime now, int core_id)
{

  RestSeg_lock->acquire();
  bool hit = m_RestSeg_cache->accessSingleLine(address, Cache::LOAD, NULL, 0, now, true);

  IntPtr tag;
  UInt32 set_index;
  m_RestSeg_cache->splitAddress(address, tag, set_index);

  // if(hit == true) std::cout <<"Hit in tag: " << tag << " for core id" << core_id << std::endl;
  bool owner_hit = false;
  // std::cout << "Starting search for owner" << std::endl;
  for (int i = 0; i < m_RestSeg_cache->getCacheSet(set_index)->getAssociativity(); i++)
  {
    // std::cout << "Tag of set: " << set_index << " is " << m_RestSeg_cache->peekBlock(set_index,i)->getTag() << std::endl;
    // if(m_RestSeg_cache->peekBlock(set_index,i)->getTag() == tag)
    // std::cout <<"Hit in tag: " << tag <<  " The owner of block with tag : " << m_RestSeg_cache->peekBlock(set_index,i)->getTag() << " is " <<  m_RestSeg_cache->peekBlock(set_index,i)->getOwner() << std::endl;
    if ((m_RestSeg_cache->peekBlock(set_index, i)->getTag() == tag) && (m_RestSeg_cache->peekBlock(set_index, i)->getOwner() == (core_id + 1)))
    {
      // std::cout <<"Hit in tag" << tag <<  " The owner of block with tag : " << m_RestSeg_cache->peekBlock(set_index,i)->getTag() << " is " <<  m_RestSeg_cache->peekBlock(set_index,i)->getOwner() << std::endl;
      owner_hit = true;
      break;
    }
  }

  RestSeg_lock->release();

  return (owner_hit);
}

IntPtr RestSeg::calculate_permission_address(IntPtr address, int core_id)
{

  IntPtr tag;
  UInt32 set_index;
  m_RestSeg_cache->splitAddress(address, tag, set_index);
  return (IntPtr)(permissions[core_id] + (set_index * filter_size) / 8);
}

IntPtr RestSeg::calculate_tag_address(IntPtr address, int core_id)
{

  IntPtr tag;
  UInt32 set_index;
  m_RestSeg_cache->splitAddress(address, tag, set_index);
  // std::cout << "Accessing tag " <<  tags[core_id]+set_index*assoc*(metadata_size/8) << std::endl;
  return (IntPtr)(tags[core_id] + set_index * assoc * tag_size); // We assume 4byte tags including read/write permissions + PCID
}

bool RestSeg::permission_filter(IntPtr address, int core_id)
{

  RestSeg_lock->acquire();

  IntPtr tag;
  UInt32 set_index;
  bool set_is_empty = true;

  m_RestSeg_cache->splitAddress(address, tag, set_index);

  for (int i = 0; i < m_RestSeg_cache->getCacheSet(set_index)->getAssociativity(); i++)
  {

    // Check if set is empty from blocks
    if (m_RestSeg_cache->getCacheSet(set_index)->peekBlock(i)->isValid() && m_RestSeg_cache->getCacheSet(set_index)->peekBlock(i)->getOwner() == (core_id + 1))
    {
      set_is_empty = false;
    }
  }

  RestSeg_lock->release();

  return set_is_empty;
}

bool RestSeg::allocate(IntPtr address, SubsecondTime now, int core_id)
{

  RestSeg_lock->acquire();
  IntPtr tag;
  UInt32 set_index;
  bool eviction;
  IntPtr evict_addr;
  CacheBlockInfo evict_block_info;
  if (utilization.size() != 0)
  {
    if (utilization[utilization.size() - 1] / (num_sets * assoc) > 0.9)
    {
      std::cout << "RestSeg is full" << std::endl;
      RestSeg_lock->release();
      return false;
    }
  }

  m_allocations++;

  m_RestSeg_cache->splitAddress(address, tag, set_index);
  m_RestSeg_cache->insertSingleLine(address, NULL, &eviction, &evict_addr, &evict_block_info, NULL, now);

  // std::cout << "Adding address with set_index: " << set_index << " and tag: " << tag << std::endl;

  for (int i = 0; i < m_RestSeg_cache->getCacheSet(set_index)->getAssociativity(); i++)
  {

    if (m_RestSeg_cache->peekBlock(set_index, i)->getTag() == tag && m_RestSeg_cache->peekBlock(set_index, i)->getOwner() == 0)
    {
      // std::cout << "Found where we inserted the block in RestSeg" << std::endl;
      m_RestSeg_cache->peekBlock(set_index, i)->setOwner(core_id + 1); // Set core_id as owner of the page to track the permission_filter correctly
                                                                       //  std::cout << "Set the owner to: " <<  m_RestSeg_cache->peekBlock(set_index,i)->getOwner() << std::endl;

      break;
    }
  }

  if (eviction)
    m_RestSeg_conflicts++;

  RestSeg_lock->release();

  return true;
}

void RestSeg::track_utilization()
{

  RestSeg_lock->acquire();

  int accum = 0;

  for (uint32_t set_index = 0; set_index < m_RestSeg_cache->getNumSets(); ++set_index)
  {
    for (int i = 0; i < m_RestSeg_cache->getCacheSet(set_index)->getAssociativity(); i++)
    {

      if (m_RestSeg_cache->getCacheSet(set_index)->peekBlock(i)->isValid())
        accum += 1;
    }
  }

  utilization.push_back(accum);

  printf("RestSeg utilization = %d, %d, %d \n", accum, m_RestSeg_cache->getNumSets(), m_RestSeg_cache->getAssociativity());

  RestSeg_lock->release();
}

Utopia::Utopia()
{

  page_faults = 0;
  shadow_mode_enabled = Sim()->getCfg()->getBool("perf_model/utopia/shadow_mode_enabled");

  if (shadow_mode_enabled)
  {

    int RestSeg_size = Sim()->getCfg()->getInt("perf_model/utopia/shadow_RestSeg/size");
    int RestSeg_page_size = Sim()->getCfg()->getInt("perf_model/utopia/shadow_RestSeg/page_size");
    int RestSeg_assoc = Sim()->getCfg()->getInt("perf_model/utopia/shadow_RestSeg/assoc");
    String RestSeg_repl = Sim()->getCfg()->getString("perf_model/utopia/shadow_RestSeg/repl");
    String RestSeg_hash = Sim()->getCfg()->getString("perf_model/utopia/shadow_RestSeg/hash");
    shadow_RestSeg = new RestSeg(-1, RestSeg_size, RestSeg_page_size, RestSeg_assoc, RestSeg_repl, RestSeg_hash);
    registerStatsMetric("utopia", 0, "page_faults", &page_faults);

    return;
  }

  RestSegs = Sim()->getCfg()->getInt("perf_model/utopia/RestSegs");

  heur_type_primary = (Utopia::utopia_heuristic)Sim()->getCfg()->getInt("perf_model/utopia/heuristic_primary");
  heur_type_secondary = (Utopia::utopia_heuristic)Sim()->getCfg()->getInt("perf_model/utopia/heuristic_secondary");

  std::cout << std::endl;
  std::cout << "------ [VirtuOS: START] U T O P I A -------" << std::endl;
  std::cout << std::endl;

  std::cout << "[UTOPIA] Heuristic primary  " << heur_type_primary << std::endl;
  std::cout << "[UTOPIA] Heuristic secondary  " << heur_type_secondary << std::endl;

  tlb_eviction_thr = (UInt32)(Sim()->getCfg()->getInt("perf_model/utopia/tlb_eviction_thr"));
  pte_eviction_thr = (UInt32)(Sim()->getCfg()->getInt("perf_model/utopia/pte_eviction_thr"));

  std::cout << "[UTOPIA] TLB threshold:  " << tlb_eviction_thr << std::endl; // We were testing an additional technique that would migrate the PTEs of the pages that were evicted from the TLB
  std::cout << "[UTOPIA] PTE threshold:  " << pte_eviction_thr << std::endl;

  for (int i = 0; i < RestSegs; i++)
  {
    int RestSeg_size = Sim()->getCfg()->getIntArray("perf_model/utopia/RestSeg/size", i);
    int RestSeg_page_size = Sim()->getCfg()->getIntArray("perf_model/utopia/RestSeg/page_size", i);
    int RestSeg_assoc = Sim()->getCfg()->getIntArray("perf_model/utopia/RestSeg/assoc", i);
    String RestSeg_repl = Sim()->getCfg()->getStringArray("perf_model/utopia/RestSeg/repl", i);
    String RestSeg_hash = Sim()->getCfg()->getStringArray("perf_model/utopia/RestSeg/hash", i);

    RestSeg *RestSeg_object;

    RestSeg_object = new RestSeg(i + 1, RestSeg_size, RestSeg_page_size, RestSeg_assoc, RestSeg_repl, RestSeg_hash);
    RestSeg_vector.push_back(RestSeg_object);
  }

  std::cout << std::endl;
  std::cout << "------ [VirtuOS: END] U T O P I A -------" << std::endl;
  std::cout << std::endl;
}
