#include <iostream>
#include <cmath>
#include "pagesize_predictor.h"
#include "pagesize_predictor_base.h"

// Template for the PageSizePredictor class
namespace ParametricDramDirectoryMSI
{

	PageSizePredictor::PageSizePredictor(String _name, Core *core)
		: PageSizePredictorBase(_name, core)
	{
		for (int i = 0; i < 4; ++i)
		{
			history.push_back(12); // 12 for 4KB
		}

		tables.resize(16, std::vector<bool>(32, false));
	}

	int PageSizePredictor::predictPageSize(IntPtr virtual_address)
	{
		/*
			@kanellok: Add your prediction logic here.
			Make sure you return the predicted page size in "bits".
			For example 12 for 4KB pages and 21 for 2MB pages.
		*/
		unsigned int history_index = get_history_index();
		unsigned int table_index = (virtual_address >> 12) & 0x1F; // Use bits [16:12] for 32-entry table
		bool prediction = tables[history_index][table_index];
		return prediction ? 21 : 12; // true for 2MB, false for 4KB
		return page_size_list[0];	 // Placeholder: return the first page size as a default prediction
	}
	void PageSizePredictor::update(IntPtr virtual_address, int page_size)
	{
		/*
			@kanellok: Add your update logic here.
			This function should be called after a memory access to update the predictor state.
			You can use this function to adjust the prediction based on the actual page size used.
		*/
		unsigned int history_index = get_history_index();
		unsigned int table_index = (virtual_address >> 12) & 0x1F;
		tables[history_index][table_index] = (page_size == 21);

		// Update history
		history.pop_front();
		history.push_back(page_size);
	}
}