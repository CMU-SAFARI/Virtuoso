// MetricsRegistry.cpp
#include "register_metrics.h"
#include <fstream>

void MetricsRegistry::writeToFile(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + filepath);
    }

    for (const auto& pair : metrics) {
        file << pair.first << " = " << pair.second->toString() << "\n";
        
    }
}
/* Verbosity default: true preserves the standalone kernel's narration.
   mimicos::Kernel::create() overrides it for embedded use. */
namespace mimicos_log {
bool verbose = true;
}
