#pragma once

#include "pagesize_predictor_base.h"
#include "pagesize_predictor.h"

namespace ParametricDramDirectoryMSI
{
	class PagesizePredictorFactory
	{
	public:
		// Factory method to create a PageSizePredictor instance based on the name
		static PageSizePredictorBase *createDefaultPredictor(String name, Core *core)
		{
			return new PageSizePredictor(name, core);
		}

		static PageSizePredictorBase *createPagesizePredictor(String name, Core *core)
		{
			if (name == "default")
			{
				return createDefaultPredictor("default_predictor", core);
			}
			else if (name == "none")
			{
				return NULL;
			}
			else
			{
				std::cout << "[CONFIG ERROR] No such Pagesize Predictor: " << name << std::endl;
			}
			return NULL;
		}
	};
};