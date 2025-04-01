

#include "tlb_subsystem.h"
#include "tlb.h"
#include <boost/algorithm/string.hpp>
#include "config.hpp"
#include "stride_prefetcher.h"
#include "tlb_prefetcher_factory.h"
#include "pagesize_predictor_factory.h"
#include "dvfs_manager.h"

using namespace boost::algorithm;
using namespace std;

namespace ParametricDramDirectoryMSI
{

	TLBHierarchy::TLBHierarchy(String mmu_name, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model)
	{
		
            std::cout << "[MMU] Instantiating TLB Hierarchy" << std::endl;
            numLevels = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_subsystem/number_of_levels");
            
            prefetch_enabled = Sim()->getCfg()->getBool("perf_model/"+mmu_name+"/tlb_subsystem/prefetch_enabled");
            int add_extra_level = 0;

            if (prefetch_enabled)
                add_extra_level = 1;

        
            tlbLevels.resize(numLevels + add_extra_level);
            data_path.resize(numLevels + add_extra_level);
            instruction_path.resize(numLevels + add_extra_level);
            tlb_latencies.resize(numLevels + add_extra_level);

        for (int level = 1; level <= numLevels; ++level)
        {
            std::string level_str = std::to_string(level);
            String levelString = String(level_str.begin(), level_str.end());

            int numTLBs = (Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/number_of_tlbs"));

            tlbLevels[level - 1].reserve(numTLBs);
            data_path[level - 1].reserve(numTLBs);
            instruction_path[level - 1].reserve(numTLBs);
            tlb_latencies[level - 1].reserve(numTLBs);

            for (int tlbIndex = 1; tlbIndex <= numTLBs; ++tlbIndex)
            {

                std::string tlbIndex_str = std::to_string(tlbIndex);

                String tlbIndexString = String(tlbIndex_str.begin(), tlbIndex_str.end());
                String tlbconfigstring = "perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString;
                String type = Sim()->getCfg()->getString("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/type");
                int size = Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/size");
                int assoc = Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/assoc");
                page_sizes = Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/page_size");
                int *page_size_list = (int *)malloc(sizeof(int) * (page_sizes));
                bool allocate_on_miss = Sim()->getCfg()->getBool("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/allocate_on_miss");

                ComponentLatency latency = ComponentLatency(core ? core->getDvfsDomain() : Sim()->getDvfsManager()->getGlobalDomain(DvfsManager::DvfsGlobalDomain::DOMAIN_GLOBAL_DEFAULT), Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/access_latency"));

                for (int i = 0; i < page_sizes; i++)
                    page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + mmu_name + "/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/page_size_list", i);

                std::string tlbName = "TLB_L" + std::to_string(level) + "_" + std::to_string(tlbIndex);
                String tlbname = String(tlbName.begin(), tlbName.end());
                String full_name = mmu_name + "_" + tlbname;

                TLB *tlb = new TLB(full_name, tlbconfigstring, core ? core->getId() : 0, latency, size, assoc, page_size_list, page_sizes, type, allocate_on_miss);

                tlbLevels[level - 1].push_back(tlb);

                if (type == "Data")
                    data_path[level - 1].push_back(tlb);
                else if (type == "Instruction")
                    instruction_path[level - 1].push_back(tlb);
                else
                {
                    data_path[level - 1].push_back(tlb);
                    instruction_path[level - 1].push_back(tlb);
                }
            }
        
            if (prefetch_enabled)
            {
                std::cout << "Prefetch Enabled" << std::endl;
                int numpqs = (Sim()->getCfg()->getInt("perf_model/tlb_prefetch/number_of_pqs"));
                tlbLevels[numLevels].reserve(numpqs);
                data_path[numLevels].reserve(numpqs);
                instruction_path[numLevels].reserve(numpqs);
                tlb_latencies[numLevels].reserve(numpqs);
                for (int pqIndex = 1; pqIndex <= numpqs; pqIndex++)
                {
                    std::string pqIndex_str = std::to_string(pqIndex);
                    String pqIndexString = String(pqIndex_str.begin(), pqIndex_str.end());
                    String pqconfigstring = "perf_model/" + mmu_name + "/tlb_prefetch/pq" + pqIndexString;
                    String type = Sim()->getCfg()->getString("perf_model/tlb_prefetch/pq" + pqIndexString + "/type");

                    int *page_size_list = (int *)malloc(sizeof(int) * 2);
                    page_size_list[0] = 12;
                    page_size_list[1] = 21;

                    int size = Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_prefetch/pq" + pqIndexString + "/size");
                    std::string pqName = "PQ_" + std::to_string(pqIndex);
                    String pqname = String(pqName.begin(), pqName.end());
                    int number_of_prefetchers = Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_prefetch/pq" + pqIndexString + "/number_of_prefetchers");
                    TLBPrefetcherBase **prefetchers = (TLBPrefetcherBase **)malloc(sizeof(TLBPrefetcherBase *) * number_of_prefetchers);
                    std::cout << "TLB Prefetchers: " << number_of_prefetchers << std::endl;

                    for (int i = 0; i < number_of_prefetchers; i++)
                    {
                        String name = Sim()->getCfg()->getStringArray("perf_model/" + mmu_name + "/tlb_prefetch/pq" + pqIndexString + "/prefetcher_list", i);
                        prefetchers[i] = TLBprefetcherFactory::createTLBPrefetcher(name, pqIndexString, core, memory_manager, shmem_perf_model);
                    }

                    ComponentLatency latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + mmu_name + "/tlb_prefetch/pq" + pqIndexString + "/access_latency"));

                    TLB *pq = new TLB(pqname, pqconfigstring, core->getId(), latency, size, 1, page_size_list, 2, type, false, true, prefetchers, number_of_prefetchers);

                    tlbLevels[numLevels].push_back(pq);
                    if (type == "Data")
                        data_path[numLevels].push_back(pq);
                    else if (type == "Instruction")
                        instruction_path[numLevels].push_back(pq);
                    else
                    {
                        data_path[numLevels].push_back(pq);
                        instruction_path[numLevels].push_back(pq);
                    }
                }
                numLevels++;
            }
        }

    }

    int TLBHierarchy::predictPagesize(IntPtr eip)
    {
        if (page_size_predictor == NULL)
        {
            if (page_sizes == 0)
            {
                return 0;
            }
            return 12;
        }
        return page_size_predictor->predictPageSize(eip);
    }

    void TLBHierarchy::predictionResult(IntPtr eip, bool success)
    {
        if (page_size_predictor == NULL)
        {
            return;
        }
        return page_size_predictor->predictionResult(eip, success);
    }
    TLBHierarchy::~TLBHierarchy()
    {
    }

}
