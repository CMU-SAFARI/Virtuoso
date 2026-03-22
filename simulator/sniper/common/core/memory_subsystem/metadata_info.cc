#include "metadata_info.h"

// Define the per-core storage
MetadataInfo MetadataContext::s_core_info[MetadataContext::MAX_CORES];
bool MetadataContext::s_core_info_valid[MetadataContext::MAX_CORES] = {false};
