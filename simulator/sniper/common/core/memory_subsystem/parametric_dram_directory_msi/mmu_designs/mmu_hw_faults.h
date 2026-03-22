
#pragma once
#include "../memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu_base.h"
#include "metadata_table_base.h"
#include "ptmshrs.h"
#include "base_filter.h"

namespace ParametricDramDirectoryMSI
{
	
	class TLBHierarchy;

    class HWFaultHandler
    {

    public:
        IntPtr start_ppn;
        IntPtr end_ppn;

        int number_of_pages;
        IntPtr *tag_array;
        int page_size = 12; // 4KB pages

        IntPtr tag_array_start_ppn;
        IntPtr tag_array_end_ppn;

        HWFaultHandler(IntPtr start_address, IntPtr end_address, int page_size)
        {
            start_ppn = start_address >> page_size;
            end_ppn = end_address >> page_size;
            number_of_pages = end_ppn - start_ppn + 1;

            tag_array = new IntPtr[number_of_pages];
            for (int i = 0; i < number_of_pages; i++){
                tag_array[i] = static_cast<IntPtr>(-1);
            }

        }
        ~HWFaultHandler(){
            delete[] tag_array;
        }
        
        IntPtr translateAddress(IntPtr address){
            IntPtr ppn = (address >> page_size) % number_of_pages;
            if (tag_array[ppn] == (address >> page_size)){
                return tag_array[ppn];
            }
            return static_cast<IntPtr>(-1);
        }
        IntPtr handleFault(IntPtr address){

            if (translateAddress(address) != static_cast<IntPtr>(-1)){
                return translateAddress(address);
            }

            IntPtr ppn = (address >> page_size) % number_of_pages;
            if (tag_array[ppn] != static_cast<IntPtr>(-1)){
                tag_array[ppn] = address >> page_size;
                return tag_array[ppn];
            }
            return static_cast<IntPtr>(-1);
        }

    
    };

	class MemoryManagementUnitHWFault : public MemoryManagementUnitBase
	{

	private:
		MemoryManagerBase *memory_manager;
		TLBHierarchy *tlb_subsystem;
		MetadataTableBase *metadata_table;
		MSHR *pt_walkers; 
		BaseFilter *ptw_filter;
        HWFaultHandler hw_fault_handler;
		//For the log
		std::ofstream log_file;
		std::string log_file_name;

		struct
		{
			UInt64 num_translations;
			UInt64 page_faults;
			UInt64 page_table_walks;
			SubsecondTime total_walk_latency;
			SubsecondTime total_translation_latency;
			SubsecondTime total_tlb_latency;
			SubsecondTime total_fault_latency;
			SubsecondTime walker_is_active;
			SubsecondTime *tlb_latency_per_level;
			UInt64 *tlb_hit_page_sizes; 

		} translation_stats;
		
	public:
		MemoryManagementUnitHWFault(Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);
		~MemoryManagementUnitHWFault();

		void instantiatePageTableWalker();
		void instantiateMetadataTable();
		void instantiateTLBSubsystem();
        void instantiateHWFaultHandler();
		void registerMMUStats();
		void discoverVMAs();
		
		BaseFilter *getPTWFilter() override { return ptw_filter; }

		PTWResult filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count);
		IntPtr performAddressTranslation(IntPtr eip, IntPtr virtual_address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		
	};

}