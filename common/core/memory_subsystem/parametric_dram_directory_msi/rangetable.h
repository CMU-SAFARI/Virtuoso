
#pragma once
#include <iostream>
#include <vector>
#include <utility>
#include "core.h"
#include "fixed_types.h"
#include "stats.h"
#include "simulator.h"

namespace ParametricDramDirectoryMSI
{

    struct RangeEntry
    {
        IntPtr offset;
    };

    class TreeNode
    {
        std::pair<uint64_t, uint64_t> *keys;
        RangeEntry *values;
        int t;
        TreeNode **C;
        int n;
        bool leaf;

    public:
        TreeNode(int temp, bool bool_leaf);

        void insertNonFull(std::pair<uint64_t, uint64_t> k);
        void splitChild(int i, TreeNode *y);
        void traverse();

        TreeNode *search(IntPtr address, std::vector<IntPtr> &accessedAddresses);

        friend class RangeTableBtree;
    };

    class RangeTable
    {
    private:
        String name;
        struct Stats
        {
            uint64_t range_walks;
            uint64_t accesses;

        } rt_stats;

    public:
        RangeTable(String name, Core *core)
        {
            this->name = name;
            bzero(&rt_stats, sizeof(rt_stats));
            registerStatsMetric(name, core->getId(), "range_walks", &rt_stats.range_walks);
            registerStatsMetric(name, core->getId(), "accesses", &rt_stats.accesses);
        }
        virtual void insert(std::pair<uint64_t, uint64_t> key, RangeEntry value) = 0;
        virtual std::pair<TreeNode *, std::vector<IntPtr>> lookup(uint64_t address) = 0;
        ~RangeTable();
    };

}
