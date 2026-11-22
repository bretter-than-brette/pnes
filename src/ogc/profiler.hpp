/**
 * @file profiler.hpp
 * @brief API of the on-target PC sampler. Declared unconditionally -- a header
 *        has no config knowledge -- but with OGC_PROFILER off nothing calls it.
 */
#ifndef OGC_PROFILER_H
#define OGC_PROFILER_H

#include <intsh>
using namespace br0::intsh;

struct ProfEntry { u32 addr; u32 count; };

struct ProfStats {
    u32 total;     ///< counted samples (main thread RUNNING)
    u32 idle;      ///< skipped samples (main thread WAITING / blocked)
    u32 dropped;   ///< samples discarded because the histogram was full
};

void prof_init();

void prof_reset();

int prof_top(ProfEntry* out, int max, ProfStats* stats);

#endif // OGC_PROFILER_H
