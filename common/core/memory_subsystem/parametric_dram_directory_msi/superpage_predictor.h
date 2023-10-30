#pragma once
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagesize_predictor_base.h"

namespace ParametricDramDirectoryMSI
{

	class SuperpagePredictor : public PageSizePredictorBase
	{
		typedef struct entry_predictor_t
		{
			uint saturation_bits : 2;
		} entry_predictor;

	public:
		entry_predictor *predictor_table;
		int small_page_size;
		int large_page_size;
		int table_size;
		SuperpagePredictor(int small_page_size, int large_page_size, int table_size);
		int predictPageSize(IntPtr eip);
		void predictionResult(IntPtr eip, bool result);
	};
}