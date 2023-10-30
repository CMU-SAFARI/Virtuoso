#ifndef __UTOPIA_H
#define __UTOPIA_H

#include "stats.h"
#include "config.hpp"
#include "simulator.h"
#include <cmath>
#include <iostream>
#include <utility>
#include "cache.h"
#include <vector>
#include "stats.h"
#include "utils.h"
#include "cache_set.h"
#include "hash_map_set.h"
#include "physical_memory_allocator.h"

class RestSeg
{

        // @kanellok: Add Replacement Policy in a RestSeg
private:
        int id;
        long long int size;
        int page_size; // RestSeg can be 4KB, 2MB, 1GB
        uint assoc;
        String hash;
        String repl;
        int num_sets;
        int TAR_size;
        int SF_size;
        int filter_size;
        int tag_size;

        UInt64 m_RestSeg_conflicts,
            m_RestSeg_accesses, m_RestSeg_hits, m_allocations, m_pagefaults;

        std::unordered_map<IntPtr, bool> vpn_container; // container to check if vpn is inside RestSeg

        IntPtr RestSeg_tags_base; // Used to perform mem access
        IntPtr RestSeg_permissions_base;

        std::vector<IntPtr> permissions;
        std::vector<IntPtr> tags;

        int *RestSeg_allocation_heatmap;

public:
        std::vector<int> utilization;
        Cache *m_RestSeg_cache;
        Lock *RestSeg_lock; // We need to lock RestSeg every time we read/modify

        RestSeg(int id, int size, int page_size, int assoc, String repl, String hash_function);
        ~RestSeg();

        int getSize() { return size; }
        std::unordered_map<IntPtr, bool> getVpnContainer() { return vpn_container; }
        int getPageSize() { return page_size; }
        int getAssoc() { return assoc; }
        std::vector<IntPtr> getPermissionsVector() { return permissions; }
        IntPtr getPermissions() { return RestSeg_permissions_base; }
        IntPtr getTags() { return RestSeg_tags_base; }
        bool inRestSeg(IntPtr address, bool count, SubsecondTime now, int core_id);
        bool inRestSegnostats(IntPtr address, bool count, SubsecondTime now, int core_id);
        bool allocate(IntPtr address, SubsecondTime now, int core_id);
        bool permission_filter(IntPtr address, int core_id);
        IntPtr calculate_permission_address(IntPtr address, int core_id);
        IntPtr calculate_tag_address(IntPtr address, int core_id);
        void track_utilization();
};

class Utopia
{
private:
        std::vector<RestSeg *> RestSeg_vector;
        RestSeg *shadow_RestSeg;

public:
        int RestSegs;

        enum utopia_heuristic
        {
                none = 0,
                tlb,
                pte,
                pf
        };

        utopia_heuristic heur_type_primary;
        utopia_heuristic heur_type_secondary;

        int pte_eviction_thr;
        int tlb_eviction_thr;

        bool shadow_mode_enabled;
        UInt64 page_faults;
        PhysicalMemoryAllocator *physical_memory_allocator;
        Utopia();

        std::vector<RestSeg *> getRestSegVector() { return RestSeg_vector; }
        RestSeg *getRestSeg(int index) { return RestSeg_vector[index]; }
        RestSeg *getShadowRestSeg() { return shadow_RestSeg; }

        utopia_heuristic getHeurPrimary() { return heur_type_primary; }
        utopia_heuristic getHeurSecondary() { return heur_type_secondary; }

        void increasePageFaults() { page_faults++; }

        int getPTEthr() { return pte_eviction_thr; }
        int getTLBthr() { return tlb_eviction_thr; }
        bool getShadowMode() { return shadow_mode_enabled; }

        static const UInt64 HASH_PRIME = 124183;
};

#endif
