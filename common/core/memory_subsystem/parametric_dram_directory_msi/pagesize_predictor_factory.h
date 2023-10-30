#pragma once

#include "pagesize_predictor_base.h"
#include "superpage_predictor.h"

namespace ParametricDramDirectoryMSI
{
	class PagesizePredictorFactory
	{
	public:
		static PageSizePredictorBase *createSuperpagePrefetcher(String name)
		{
			int small_page_size = Sim()->getCfg()->getInt("perf_model/superpage/small_page_size");

			int large_page_size = Sim()->getCfg()->getInt("perf_model/superpage/large_page_size");

			int table_size = Sim()->getCfg()->getInt("perf_model/superpage/table_size");
			return new SuperpagePredictor(small_page_size, large_page_size, table_size);
		}
		static PageSizePredictorBase *createPagesizePredictor(String name)
		{
			if (name == "superpage")
			{
				return createSuperpagePrefetcher(name);
			}
			else if (name == "none")
			{
				return NULL;
			}
			else
			{
				std::cout << "[CONFIG ERROR] No such Pagesize Predictor: " << name << std::endl;
			}
		}
	};
}