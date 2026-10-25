#pragma once

// Compile time switches for the patches game-patcher applies to the game process. Each one can be turned off on its own
// by setting it to 0 here or on the compiler command line (e.g. /DGAME_PATCHER_DEVICE_LOCK=0). A disabled patch is not
// applied at all, so the game keeps its stock code for it.

#ifndef GAME_PATCHER_RENDER_RATE
#define GAME_PATCHER_RENDER_RATE 1
#endif

#ifndef GAME_PATCHER_PACE_SLEEP
#define GAME_PATCHER_PACE_SLEEP 1
#endif

#ifndef GAME_PATCHER_PERF_SCOPE_SKIP
#define GAME_PATCHER_PERF_SCOPE_SKIP 1
#endif

#ifndef GAME_PATCHER_DEVICE_LOCK
#define GAME_PATCHER_DEVICE_LOCK 1
#endif

#ifndef GAME_PATCHER_SSE_ROUNDING
#define GAME_PATCHER_SSE_ROUNDING 1
#endif

#ifndef GAME_PATCHER_CELL_CENTER
#define GAME_PATCHER_CELL_CENTER 1
#endif

#ifndef GAME_PATCHER_CROWD_COST_MEMO
#define GAME_PATCHER_CROWD_COST_MEMO 1
#endif

#ifndef GAME_PATCHER_CROWD_COST_REWRITE
#define GAME_PATCHER_CROWD_COST_REWRITE 1
#endif

#ifndef GAME_PATCHER_STATS
#define GAME_PATCHER_STATS 0
#endif

namespace features {

    // Draws at 60 frames per second without changing the game pace or the camera speed (render_rate.cpp).
    constexpr bool RENDER_RATE = GAME_PATCHER_RENDER_RATE != 0;

    // The frame limiter sleeps through most of the time it has to wait instead of spinning on Sleep(0) (pace_sleep.cpp).
    // Same frame rate, but the main thread no longer pins a core.
    constexpr bool PACE_SLEEP = GAME_PATCHER_PACE_SLEEP != 0;

    // Skips building the profiler event names of the render scopes, which nobody reads (perf_scope_skip.cpp).
    constexpr bool PERF_SCOPE_SKIP = GAME_PATCHER_PERF_SCOPE_SKIP != 0;

    // Replaces the kernel mutex that guards every render call with a user-mode lock (device_lock.cpp).
    constexpr bool DEVICE_LOCK = GAME_PATCHER_DEVICE_LOCK != 0;

    // Rounds with SSE2 instead of msvcr71's floor and ceil, which switch the x87 rounding mode on every call (sse_rounding.cpp).
    constexpr bool SSE_ROUNDING = GAME_PATCHER_SSE_ROUNDING != 0;

    // Computes the pathfinder's cell centre once per crowd cost call instead of once per unit in the window (cell_center.cpp).
    constexpr bool CELL_CENTER = GAME_PATCHER_CELL_CENTER != 0;

    // Remembers the crowd cost of a cell for the duration of one pathfinder search (crowd_cost_memo.cpp).
    constexpr bool CROWD_COST_MEMO = GAME_PATCHER_CROWD_COST_MEMO != 0;

    // Computes the pathfinder's crowd cost from one cached record per unit and search instead of asking the game's functions
    // about every unit in every cell window (crowd_cost_memo.cpp).
    constexpr bool CROWD_COST_REWRITE = GAME_PATCHER_CROWD_COST_REWRITE != 0;

    // Makes the patches count what they do in a shared memory block, for measuring. Leave off for release builds.
    constexpr bool STATS = GAME_PATCHER_STATS != 0;
}
