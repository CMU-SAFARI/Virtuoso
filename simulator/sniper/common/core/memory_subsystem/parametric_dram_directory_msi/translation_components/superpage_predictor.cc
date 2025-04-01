#include <iostream>
#include <cmath>
#include "superpage_predictor.h"
#include "pagesize_predictor_base.h"
namespace ParametricDramDirectoryMSI
{

	SuperpagePredictor::SuperpagePredictor(int _small_page_size, int _large_page_size, int _table_size) : PageSizePredictorBase(), small_page_size(_small_page_size), large_page_size(_large_page_size), table_size(_table_size)
	{
		table_size = std::pow(2, _table_size);

		predictor_table = (entry_predictor *)malloc(table_size * sizeof(entry_predictor));
		for (int i = 0; i < table_size; i++)
		{
			predictor_table[i].saturation_bits = 0;
		}
	}
	int SuperpagePredictor::predictPageSize(IntPtr eip)
	{
		int index = eip % table_size;
		if (predictor_table[index].saturation_bits < 2)
		{
			return small_page_size;
		}
		return large_page_size;
	}
	void SuperpagePredictor::predictionResult(IntPtr eip, bool result)
	{
		int index = eip % table_size;
		if (result)
		{
			if (predictor_table[index].saturation_bits != 3)
			{
				predictor_table[index].saturation_bits++;
			}
		}
		else
		{
			if (predictor_table[index].saturation_bits != 0)
			{
				predictor_table[index].saturation_bits--;
			}
		}
	}
}