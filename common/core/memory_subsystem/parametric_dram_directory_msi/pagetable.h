
#pragma once
#include "fixed_types.h"
#include "cache.h"
#include <unordered_map>
#include <random>
#include "pwc.h"
#include <bitset>

using namespace std;

namespace ParametricDramDirectoryMSI
{
	typedef std::vector<tuple<int, int, IntPtr, bool>> accessedAddresses;
	typedef tuple<int, accessedAddresses, IntPtr> PTWResult; //<pagesize, accessedAddresses, ppn>
	typedef int PageSize;
	typedef tuple<int, accessedAddresses, IntPtr> AllocatedPage; //<pagesize, accessedAddresses, ppn>

	class PageTable
	{

	protected:
		int core_id;
		String name;
		int *m_page_size_list;
		int m_page_sizes;

	public:
		struct
		{
			UInt64 page_walks;
		} stats;

		PageTable(int core_id, String name, int page_sizes, int *page_size_list)
		{
			this->m_page_sizes = page_sizes;
			this->m_page_size_list = page_size_list;
			this->core_id = core_id;
			this->name = name;
		};

		virtual PTWResult initializeWalk(IntPtr address, bool count) = 0;
		virtual AllocatedPage handlePageFault(IntPtr address, bool count, IntPtr ppn = -1) = 0;
		int *getPageSizes() { return m_page_size_list; };
		int getPageSizesCount() { return m_page_sizes; };
		virtual void deletePage(IntPtr address){};
		virtual void insertLargePage(IntPtr address, IntPtr ppn){};

		virtual void promotePage(IntPtr address, std::bitset<512> pages, IntPtr ppn)
		{
			for (int i = 0; i < 512; i++)
			{
				if (pages[i])
				{
					deletePage((((address >> 21) * 512) + i) * 4096);
				}
			}

			insertLargePage(address, ppn);
		};
	};

}
