
#pragma once
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"

namespace ParametricDramDirectoryMSI
{
	class PageSizePredictorBase
	{
	public:
		PageSizePredictorBase(){};
		virtual int predictPageSize(IntPtr eip) = 0;
		virtual void predictionResult(IntPtr eip, bool result) = 0;
	};
}