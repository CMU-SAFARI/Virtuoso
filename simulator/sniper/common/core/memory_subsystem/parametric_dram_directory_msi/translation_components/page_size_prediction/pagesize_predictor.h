#pragma once
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagesize_predictor_base.h"

namespace ParametricDramDirectoryMSI
{

	class PageSizePredictor : public PageSizePredictorBase
	{

		struct
		{
			// Statistics for the page size predictor
			int correct_predictions; // Number of correct predictions
			int total_predictions;
		} stats;

	public:
		PageSizePredictor(String name, Core *core);
		int predictPageSize(IntPtr virtual_address);
		void update(IntPtr virtual_address, int page_size);

		std::deque<int> history;			   // Stores last 4 page sizes (12 for 4KB, 21 for 2MB)
		std::vector<std::vector<bool>> tables; // 16 tables of 32 bits

		unsigned int get_history_index()
		{
			unsigned int index = 0;
			for (int size : history)
			{
				index = (index << 1) | (size == 21);
			}
			return index;
		}
	};
}