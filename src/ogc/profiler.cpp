/**
 * @file profiler.cpp
 * @brief On-target statistical PC sampler for the libogc backend.
 *
 * Dolphin reports 100% speed with headroom while the game still hitches, which
 * means the cost is episodic CPU work in the game logic rather than a throughput
 * problem. This samples the main thread's PC at ~10 kHz and bins the hits; the
 * video backend draws the hottest addresses as a hex+bar overlay, resolved
 * offline with tools/profsym.sh.
 *
 * Mechanism (libogc's tuxedo microkernel): a thread's saved context sits at the
 * head of its TCB, so a higher-priority sampler thread wakes -- preempting main,
 * whose PC the scheduler has just saved -- and reads it. Samples are only counted
 * while main is RUNNING; blocked in VIDEO_WaitVSync its saved PC is stale idle
 * time, so the histogram reflects on-CPU work rather than spare headroom.
 *
 * Touches no exception vectors and uses only public tuxedo structs.
 */
#include "internal.hpp"
#include "profiler.hpp"

// Debug builds only, where CMake defines OGC_PROFILER; release gets an empty
// translation unit. profiler.hpp declares the API unconditionally, since a
// header has no config knowledge.
#if defined(OGC_PROFILER)

#include <tuxedo/thread.h>
#include <malloc.h>
#include <string.h>

namespace {

// Open-addressed, power-of-two, linear probe, drop-on-full: hot addresses are
// sampled early and often, so only cold late-comers are dropped -- fine for a
// top-N display.
constexpr int   HASH_SLOTS = 8192;
constexpr u32   SAMPLE_US  = 100;          // ~10 kHz sampling

struct Slot { u32 addr; u32 count; };      // addr == 0 marks an empty slot

Slot*    g_hist   = nullptr;
u32      g_total  = 0;                      // counted (RUNNING) samples
u32      g_idle   = 0;                      // skipped (WAITING) samples
u32      g_drop   = 0;                      // samples that found the table full
KThread* g_main   = nullptr;

KThread        g_sampler;
alignas(16) u8 g_sampler_stack[8192];

void record(u32 pc) {
    pc &= ~3u;                             // instruction-align
    if (!pc) return;
    u32 i = (pc >> 2) & (HASH_SLOTS - 1);
    for (int probe = 0; probe < HASH_SLOTS; probe++) {
        Slot& s = g_hist[i];
        if (s.addr == pc)  { s.count++; return; }
        if (s.addr == 0)   { s.addr = pc; s.count = 1; return; }
        i = (i + 1) & (HASH_SLOTS - 1);
    }
    g_drop++;
}

sptr sampler_fn(void*) {
    for (;;) {
        KThreadSleepUs(SAMPLE_US);
        if (g_main->state == KTHR_STATE_RUNNING) {
            g_total++;
            record(g_main->ctx.pc);
        } else {
            g_idle++;
        }
    }
    return 0;
}

}   // namespace

void prof_init() {
    g_main = KThreadGetSelf();             // must be called on the main thread
    g_hist = static_cast<Slot*>(memalign(32, HASH_SLOTS * sizeof(Slot)));
    prof_reset();
    KThreadPrepare(&g_sampler, sampler_fn, nullptr,
                   g_sampler_stack + sizeof(g_sampler_stack), 0x10);
    KThreadResume(&g_sampler);
}

void prof_reset() {
    memset(g_hist, 0, HASH_SLOTS * sizeof(Slot));
    g_total = g_idle = g_drop = 0;
}

int prof_top(ProfEntry* out, int max, ProfStats* stats) {
    if (stats) { stats->total = g_total; stats->idle = g_idle; stats->dropped = g_drop; }
    int n = 0;
    for (int k = 0; k < max; k++) {
        u32 best = 0;
        int bi   = -1;
        for (int i = 0; i < HASH_SLOTS; i++) {
            if (g_hist[i].addr == 0 || g_hist[i].count <= best) continue;
            bool taken = false;
            for (int j = 0; j < n; j++)
                if (out[j].addr == g_hist[i].addr) { taken = true; break; }
            if (taken) continue;
            best = g_hist[i].count;
            bi   = i;
        }
        if (bi < 0) break;
        out[n].addr  = g_hist[bi].addr;
        out[n].count = g_hist[bi].count;
        n++;
    }
    return n;
}

#endif // OGC_PROFILER
