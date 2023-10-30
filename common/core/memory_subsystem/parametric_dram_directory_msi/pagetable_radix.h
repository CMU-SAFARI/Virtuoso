

#include "pagetable.h"
#include "pwc.h"

namespace ParametricDramDirectoryMSI
{

	class PageTableRadix : public PageTable
	{

	private:
		struct PTFrame;

		struct PTE
		{
			bool valid;
			IntPtr ppn;
		};

		struct PTEntry
		{
			bool is_pte;
			union
			{
				PTE translation;
				PTFrame *next_level;
			} data;
		};

		struct PTFrame
		{
			PTEntry *entries;
			IntPtr emulated_ppn;
		};

		typedef struct PTFrame PT;

		PT *root;
		PWC *pwc;
		int levels;

		int m_frame_size;

		std::vector<PWC *> pwcs;

		struct Stats
		{
			UInt64 page_table_walks;
			UInt64 ptw_num_cache_accesses;
			UInt64 pf_num_cache_accesses;
			UInt64 page_faults;
			UInt64 *page_size_discovery;
			UInt64 allocated_frames;
		} stats;

	public:
		PageTableRadix(int core_id, String name, int page_sizes, int *page_size_list, int levels, int frame_size, PWC *pwc);
		PTWResult initializeWalk(IntPtr address, bool count);
		AllocatedPage handlePageFault(IntPtr address, bool count, IntPtr ppn = -1);
		void insertLargePage(IntPtr address, IntPtr ppn);
	};
}