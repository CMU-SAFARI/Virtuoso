#pragma once

#include "cache_base.h"
#include <list>
#include <unordered_map>
#include "core.h"
#include "pagetable.h"
#include "pmd_cwt_entry.h"

namespace ParametricDramDirectoryMSI
{
    struct CWCRow {
        PmdCwtEntry* cwt_entry_ptr; // Pointer to the CWT entry

        // c'tor
        CWCRow(PmdCwtEntry* entry = nullptr) : cwt_entry_ptr(entry) { }
    };

    // A simple, fully-associative LRU cache for CWC.
    class CWCache 
    {
        private:
            // stats structure
            struct
            {
                UInt64 cwc_hits;
                UInt64 cwc_accesses;
            } cwc_stats;

            // log structure
            std::ofstream log_file;
            std::string   log_file_name;

        public:
            CWCache(String name, Core* core, int size);
            // Looks for an entry. Returns true on hit, false on miss.
            bool lookup(IntPtr tag, CWCRow& entry);
            // Inserts a new entry.
            void insert(const CWCRow& entry);

        private:
            String name;
            Core* core;
            int size;
            std::list<CWCRow> m_lru_list;
            std::unordered_map<IntPtr, std::list<CWCRow>::iterator> m_map;
    };
};
