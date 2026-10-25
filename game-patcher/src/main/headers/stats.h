#pragma once

#include <cstdint>

namespace patching {

    // One call of the pathfinder's queue processing (0x006F2364), which runs once per logic frame and works off the queued path
    // requests up to a budget of cells. The head and tail words are the game's own queue indices.
    struct QueueRecord {
        uint32_t cycles;
        uint32_t cells;
        uint32_t before[4];
        uint32_t after[4];
    };

    // Counters the caves increment when features::STATS is on. They live in a file mapping called
    // "Local\GamePatcherStats", so a measuring tool can open it without knowing where the game put it.
    struct Stats {
        char magic[8];
        uint32_t deviceLockAcquires;
        uint32_t deviceLockTries;
        uint32_t deviceLockTryTimeouts;
        uint32_t deviceLockReleases;
        uint32_t perfScopesSkipped;
        uint32_t paceLoopIterations;
        uint32_t paceSleepMs;
        uint32_t floorCalls;
        uint32_t ceilCalls;
        uint32_t roundingFallbacks;
        uint32_t cellCenterComputed;
        uint32_t cellCenterReused;
        uint32_t cellCenterChecks;
        uint32_t cellCenterMismatches;
        uint32_t crowdCostCalls;
        uint32_t crowdCostHits;
        uint32_t crowdCostChecks;
        uint32_t crowdCostMismatches;
        uint32_t crowdCostCyclesLow;
        uint32_t crowdCostCyclesHigh;

        // Written by the measuring tool to compare the pathfinder optimizations within one game: 0 runs the game's own
        // code, 1 adds the SSE rounding and the cell centre reuse, 2 adds the crowd cost memo, 3 adds the rewritten crowd cost.
        // Starts at 3.
        uint32_t control;

        // Time stamp counter ticks per millisecond, to turn the cycle counts into time.
        uint32_t tscKHz;

        // Questions about units the crowd cost memo was asked; crowdCostHits of them were answered from the tables.
        uint32_t objectMemoLookups;
        uint32_t sqrtCalls;
        uint32_t sqrtFallbacks;

        // Calls of AdjustDestination and the time spent in them (pathfinder_timing.cpp).
        uint32_t adjustCalls;
        uint32_t adjustCyclesLow;
        uint32_t adjustCyclesHigh;

        // The rewritten crowd cost (crowd_cost_memo.cpp): calls it answered, calls left to the game's code because the x87
        // precision was neither 24 nor 53 bits, calls compared with the game's code, and differences (must stay 0).
        uint32_t crowdRewriteCalls;
        uint32_t crowdRewriteFallbacks;
        uint32_t crowdRewriteChecks;
        uint32_t crowdRewriteMismatches;

        // Time spent in the pathfinder's queue processing.
        uint32_t queueCyclesLow;
        uint32_t queueCyclesHigh;

        uint32_t queueWrites;
        QueueRecord queue[64];
    };

    // Creates the mapping. Returns null if statistics are off or the mapping could not be created.
    Stats *createStats();

    // The block created by createStats, or null.
    Stats *stats();
}
