#include <vector>
#include <utility>
#include "core.h"
#include "fixed_types.h"
#include "rangetable.h"
#include "rangetable_btree.h"
#include <tuple>

namespace ParametricDramDirectoryMSI
{

    TreeNode::TreeNode(int t1, bool leaf1)
    {
        t = t1;
        leaf = leaf1;

        keys = new std::pair<uint64_t, uint64_t>[2 * t - 1];
        values = new RangeEntry[2 * t - 1];
        C = new TreeNode *[2 * t];

        n = 0;
    }

    void TreeNode::traverse()
    {
        int i;
        for (i = 0; i < n; i++)
        {
            if (leaf == false)
                C[i]->traverse();
            std::cout << " " << keys[i].first << " " << keys[i].second << " " << values[i].offset << std::endl;
        }

        if (leaf == false)
            C[i]->traverse();
    }

    TreeNode *TreeNode::search(IntPtr k, std::vector<IntPtr> &accessedAddresses)
    {
        int i = 0;
        while (i < n && k > keys[i].second)
        {
            accessedAddresses.push_back((IntPtr)&keys[i]);
            i++;
        }

        if (keys[i].first < k && keys[i].second > k)
            return this;

        if (leaf == true)
            return NULL;

        return C[i]->search(k, accessedAddresses);
    }

    void RangeTableBtree::insert(std::pair<uint64_t, uint64_t> k, RangeEntry value)
    {
        if (root == NULL)
        {
            root = new TreeNode(t, true);
            root->keys[0] = k;
            root->n = 1;
        }
        else
        {
            if (root->n == 2 * t - 1)
            {
                TreeNode *s = new TreeNode(t, false);

                s->C[0] = root;

                s->splitChild(0, root);

                int i = 0;
                if (s->keys[0] < k)
                    i++;
                s->C[i]->insertNonFull(k);

                root = s;
            }
            else
                root->insertNonFull(k);
        }
    }

    void TreeNode::insertNonFull(std::pair<uint64_t, uint64_t> k)
    {
        int i = n - 1;

        if (leaf == true)
        {
            while (i >= 0 && keys[i] > k)
            {
                keys[i + 1] = keys[i];
                i--;
            }

            keys[i + 1] = k;
            n = n + 1;
        }
        else
        {
            while (i >= 0 && keys[i] > k)
                i--;

            if (C[i + 1]->n == 2 * t - 1)
            {
                splitChild(i + 1, C[i + 1]);

                if (keys[i + 1] < k)
                    i++;
            }
            C[i + 1]->insertNonFull(k);
        }
    }

    void TreeNode::splitChild(int i, TreeNode *y)
    {
        TreeNode *z = new TreeNode(y->t, y->leaf);
        z->n = t - 1;

        for (int j = 0; j < t - 1; j++)
            z->keys[j] = y->keys[j + t];

        if (y->leaf == false)
        {
            for (int j = 0; j < t; j++)
                z->C[j] = y->C[j + t];
        }

        y->n = t - 1;
        for (int j = n; j >= i + 1; j--)
            C[j + 1] = C[j];

        C[i + 1] = z;

        for (int j = n - 1; j >= i; j--)
            keys[j + 1] = keys[j];

        keys[i] = y->keys[t - 1];
        n = n + 1;
    }

}
