
#ifndef GLOBALS_H
#define GLOBALS_H

#include "INIReader.h"
#include "register_metrics.h"
#include "fixed_types.h"

extern INIReader* reader;
extern MetricsRegistry *m_stats;

namespace mimicos_log {

/**
 * Runtime verbosity for the userspace allocator policies.
 *
 * The standalone kernel narrates every allocation to stdout, which is useful
 * when it is the only thing running.  Embedded in a host simulator it is not:
 * the output drowns the simulator's own, and formatting a few lines per page
 * fault costs real wall-clock time in the host's critical path.
 *
 * Defaults to true so the standalone kernel is unchanged; the embedding
 * library turns it off unless the config asks for it.
 */
extern bool verbose;

inline bool enabled() { return verbose; }

} // namespace mimicos_log

#endif // GLOBALS_H
