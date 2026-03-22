
#include <list>
#include <unordered_map>    
#include "cwc.h"
#include "pagetable_cuckoo.h"

#undef DEBUG


using namespace std;
// Looks for an entry. Returns true on hit, false on miss.
namespace ParametricDramDirectoryMSI
{
    CWCache::CWCache(String _name, Core* _core, int _size):
                                    name(_name),
                                    core(_core),
                                    size(_size) 
    {
        m_map.reserve(_size); // Reserve space for the map

        int core_id = core->getId();
		log_file_name = "mmu_cwc.log";
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name);
		std::cout << "[CWC] ECH CWC debug info (if enabled) saved in file: " << log_file_name << '\n';
		std::cout << "[CWC] Registering ECH CWC stats with name " << name << " and core " << core_id << "\n";

        // Initialize stats
        cwc_stats = {
            .cwc_hits = 0,
            .cwc_accesses = 0,
        };

		// Track: CWC hits & CWC accesses
		registerStatsMetric(name, core_id, "cwc_hits", &cwc_stats.cwc_hits);
		registerStatsMetric(name, core_id, "cwc_accesses", &cwc_stats.cwc_accesses);
    };


    bool CWCache::lookup(IntPtr tag, CWCRow& entry) {

#ifdef DEBUG
        log_file << "[CWC] Lookup for tag: " << tag << "\n";
        log_file << "[CWC] Current tags saved in m_map" << std::endl;
        for (const auto& pair : m_map) {
            log_file << "[CWC] Tag: " << pair.first << " ";
        }
        log_file << std::endl;
#endif
        cwc_stats.cwc_accesses++; // Increment access stats

        auto it = m_map.find(tag);
        if (it == m_map.end()) {
            return false; // Miss
        }
        else {
            cwc_stats.cwc_hits++; // Increment hit stats
        }


        // Move to front (most recently used)
        m_lru_list.splice(m_lru_list.begin(), m_lru_list, it->second);
        entry = *it->second;
        return true;
    };

    // Inserts a new entry.
    void CWCache::insert(const CWCRow& entry) {

#ifdef DEBUG
        log_file << "[CWC] Insert for tag: " << entry.cwt_entry_ptr->tag << std::endl;
        for (int i = 0; i < 64; ++i) {
            if (entry.cwt_entry_ptr == nullptr) {
                log_file << "cwt_entry_ptr is null" << std::endl;
                continue;
            }

            else if (entry.cwt_entry_ptr->section_header[i] == EMPTY_CWT_SECTION_HEADER) {
                continue; // Skip if way of i_th entry is -1 (not set)
            }

            log_file << "[CWC] Section Header " << i << ": ";
            log_file << "has_4kb_page: " << static_cast<int>(entry.cwt_entry_ptr->section_header[i].has_4kb_page)
                     << ", has_2mb_page: " << static_cast<int>(entry.cwt_entry_ptr->section_header[i].has_2mb_page)
                     << ", way: " << static_cast<int>(entry.cwt_entry_ptr->section_header[i].way)
                     << std::endl;
        }
#endif

        auto it = m_map.find(entry.cwt_entry_ptr->tag);
        if (it != m_map.end()) {
            // Update existing entry and move to front
            *it->second = entry;
            m_lru_list.splice(m_lru_list.begin(), m_lru_list, it->second);
            return;
        }

        // If cache is full, evict the least recently used item
        if (static_cast<int>(m_lru_list.size()) == size) {
            IntPtr lru_tag = m_lru_list.back().cwt_entry_ptr->tag;
            m_lru_list.pop_back();
            m_map.erase(lru_tag);
        }

        // Add new item to front
        m_lru_list.push_front(entry);
        m_map[entry.cwt_entry_ptr->tag] = m_lru_list.begin();
    };
}
