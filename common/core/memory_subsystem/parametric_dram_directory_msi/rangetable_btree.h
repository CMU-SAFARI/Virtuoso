#pragma once
#include <iostream>
#include <vector>
#include <utility>
#include "core.h"
#include "fixed_types.h"
#include "rangetable.h"

namespace ParametricDramDirectoryMSI
{

    class RangeTableBtree : public RangeTable
    {
        TreeNode *root;
        int t;
        std::vector<IntPtr> accessedAddresses;

    public:
        RangeTableBtree(String name, Core *core, int temp) : RangeTable(name, core)
        {
            root = NULL;
            t = temp;
        }

        void traverse()
        {
            if (root != NULL)
                root->traverse();
        }

        std::pair<TreeNode *, std::vector<IntPtr>> lookup(IntPtr address)
        {
            accessedAddresses = std::vector<IntPtr>();
            if (root == NULL)
                return std::make_pair(root, accessedAddresses);
            else
            {
                TreeNode *result = root->search(address, accessedAddresses);
                return std::make_pair(result, accessedAddresses);
            }
        }

        void insert(std::pair<uint64_t, uint64_t> key, RangeEntry value);
    };

}
