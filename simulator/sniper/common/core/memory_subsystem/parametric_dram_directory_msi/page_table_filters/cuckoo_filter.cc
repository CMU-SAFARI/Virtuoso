
#include "cuckoo_filter.h"
#include "cwc.h"
#include "config.hpp"
#include "simulator.h"
#include "core.h"
#include "pagetable.h"
#include "pagetable_cuckoo.h"
#include <iostream>
#include <cmath>

#define DEBUG_CUCKOO_FILTER 

namespace ParametricDramDirectoryMSI
{


    // -------------- START DEBUG prints ---------------------
    static void debug_print_filtered_ptw_accesses(std::ofstream& log_file, const accessedAddresses& filtered_ptw_accesses) { 

        // typedef std::vector<PTWAccess> accessedAddresses;		  // <level of page table based on page size, depth of the page table, physical address we accessed, is it the PTE that contains the correct translation?>
        log_file << "CuckooFilter: ----------- START Filtered PTW Accesses ----------" << std::endl;
        for (const auto& access : filtered_ptw_accesses) {
            int level = access.table_level;
            int depth = access.depth;
            IntPtr physical_address = access.physical_addr;
            bool is_correct_translation = access.is_pte;
            log_file << "Level: " << level 
                      << ", Depth: " << depth 
                      << ", Physical Address: " << physical_address 
                      << ", Is Correct Translation: " << (is_correct_translation ? "Yes" : "No") 
                      << std::endl;
        }
        log_file << "CuckooFilter: ----------- END Filtered PTW Accesses ----------" << std::endl;
    }
    // -------------- END DEBUG prints ---------------------

    CuckooFilter::CuckooFilter(String _name, Core *_core):
                                BaseFilter(_name, _core)
        
    {
        int cwc_entries =  Sim()->getCfg()->getInt("perf_model/" + _name + "/cwc/entries");
        cwc = new CWCache(_name + "_cwc", _core, cwc_entries);
        
        log_file = std::ofstream();
		log_file_name = "cwc.log." + std::to_string(_core->getId());
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name.c_str());

    }

    CuckooFilter::~CuckooFilter()
    {

    }


    PTWResult CuckooFilter::filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count) 
    {

        CWCRow cwc_row;

        //Each section header stores information about 16MB pages and each row in the CWC stores 64 section headers.
        IntPtr tag = (virtual_address >> 30); // 21 bits for section header, 6 bits for row in CWC
        IntPtr offset_pointer = (virtual_address >> 24) & 0x3F;
#ifdef DEBUG_CUCKOO_FILTER
        log_file << "CuckooFilter: Filtering PTW Result for VA: " << virtual_address << std::endl;
        log_file << "CuckooFilter: Tag: " << tag << ", Offset Pointer: " << offset_pointer << std::endl;
#endif
        
        bool hit_cwc = cwc->lookup(tag, cwc_row);
        accessedAddresses original_ptw_accesses = ptw_result.accesses;
        accessedAddresses filtered_ptw_accesses;

        // Get the pointer to the page table 
        

        // Find the page size and way that delivers the correct translation
        int current_page_size = 0;
        int current_way = 0;
        int resolved_page_size = 0;
        int resolved_way = 0;

        for (auto &access : original_ptw_accesses)
        {
            if (access.table_level != current_page_size)
            {
                current_page_size = access.table_level;
                current_way = 0;
            }
            if (access.is_pte == true) {
                    resolved_page_size = (access.table_level == 0) ? 12 : 21; // 4KB or 2MB
                    resolved_way = current_way;
            }
            current_way++;
        }
#ifdef DEBUG_CUCKOO_FILTER
        log_file << "CuckooFilter: Resolved Page Size: " << resolved_page_size << ", Resolved Way: " << resolved_way << std::endl;
#endif

        if (hit_cwc) {

            if (cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_4kb_page && cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_2mb_page)
            {
                int way_counter = 0;
                
                for (auto &access : original_ptw_accesses) 
                {
                    if( (access.table_level == 1) && (way_counter == cwc_row.cwt_entry_ptr->section_header[offset_pointer].way))
                    {
                        filtered_ptw_accesses.push_back(access);
                        way_counter++;
                    }
                    else if (access.table_level == 1) 
                    {
                        way_counter++;
                    }
                    else if (access.table_level == 0) 
                    {
                        filtered_ptw_accesses.push_back(access);
                    }
                } 


#ifdef DEBUG_CUCKOO_FILTER
            log_file << "CuckooFilter: filtered_ptw_acesses: " << std::endl;
            debug_print_filtered_ptw_accesses(log_file, filtered_ptw_accesses);
#endif
                return PTWResult(ptw_result.page_size, filtered_ptw_accesses, ptw_result.ppn, ptw_result.pwc_latency, ptw_result.fault_happened);
            }
            else if (!cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_4kb_page && cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_2mb_page)
            {
                int way_counter = 0;
                
                for (auto &access : original_ptw_accesses) 
                {
                    if( (access.table_level == 1) && (way_counter == cwc_row.cwt_entry_ptr->section_header[offset_pointer].way))
                    {
                        filtered_ptw_accesses.push_back(access);
                        way_counter++;
                    }
                    else if (access.table_level == 1) 
                    {
                        way_counter++;
                    }
                } 

#ifdef DEBUG_CUCKOO_FILTER
            log_file << "CuckooFilter: filtered_ptw_acesses: " << std::endl;
            debug_print_filtered_ptw_accesses(log_file, filtered_ptw_accesses);
#endif
                return PTWResult(ptw_result.page_size, filtered_ptw_accesses, ptw_result.ppn, ptw_result.pwc_latency, ptw_result.fault_happened);
            }
            else if (cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_4kb_page && !cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_2mb_page)
            {
                for (auto &access : original_ptw_accesses) 
                {
                    if (access.table_level == 0)
                    {
                        filtered_ptw_accesses.push_back(access);
                    }
                }
                

#ifdef DEBUG_CUCKOO_FILTER
            log_file << "CuckooFilter: filtered_ptw_acesses: " << std::endl;
            debug_print_filtered_ptw_accesses(log_file, filtered_ptw_accesses);
#endif
                return PTWResult(ptw_result.page_size, filtered_ptw_accesses, ptw_result.ppn, ptw_result.pwc_latency, ptw_result.fault_happened);
            } 
            else {
                cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_4kb_page = (resolved_page_size == 12);
                cwc_row.cwt_entry_ptr->section_header[offset_pointer].has_2mb_page = (resolved_page_size == 21);
                cwc_row.cwt_entry_ptr->section_header[offset_pointer].way = resolved_way;
                
#ifdef DEBUG_CUCKOO_FILTER
            log_file << "CuckooFilter: No filtering has been performed - returned original result" << std::endl;
#endif
                return ptw_result; // No filtering, return original result
            }
        } 
        else {
#ifdef DEBUG_CUCKOO_FILTER
            log_file << "CuckooFilter: Miss in CWC for VA: " << virtual_address << 
            "No filtering has been performed - returned original result" << std::endl;
#endif
            // Upcast the page table to cuckoo
            PageTableCuckoo *cuckoo_page_table = dynamic_cast<PageTableCuckoo *>(page_table);
            auto cwt_row = cuckoo_page_table->retrieveCWTrow(virtual_address);
            CWCRow cwc_row = cwt_row.toCWCRow();
            cwc->insert(cwc_row);

            // If the CWC miss, we do not filter the PTW result
            return ptw_result; // No filtering, return original result
        }


    }
}