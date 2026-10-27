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

#ifndef GAME_PATCHER_BROAD_PHASE_TABLE
#define GAME_PATCHER_BROAD_PHASE_TABLE 1
#endif

#ifndef GAME_PATCHER_QUAD_BATCH_CAP
#define GAME_PATCHER_QUAD_BATCH_CAP 1
#endif

#ifndef GAME_PATCHER_QUAD_BATCH_FLUSH
#define GAME_PATCHER_QUAD_BATCH_FLUSH 1
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

    // Gives the collision broad phase's first pair table 262139 buckets instead of 11131, so that the sweep's lookups no longer walk
    // chains of ten nodes or more when a big army is on the map (broad_phase_table.cpp). The game behaves exactly as before.
    constexpr bool BROAD_PHASE_TABLE = GAME_PATCHER_BROAD_PHASE_TABLE != 0;

    // Stops the 2D overlay batch (health bars and the like) from taking more than the 65535 indices its 16 bit counters can hold:
    // beyond that the game writes past the end of its dynamic index buffer and crashes when a big army is on screen
    // (quad_batch_cap.cpp). QUAD_BATCH_FLUSH decides what happens to the rectangles that no longer fit.
    constexpr bool QUAD_BATCH_CAP = GAME_PATCHER_QUAD_BATCH_CAP != 0;

    // With the cap: draws the batch collected so far and starts a new one instead of dropping the rectangles that no longer
    // fit, so that every health bar stays visible (quad_batch_cap.cpp). Without it the excess rectangles are not drawn.
    constexpr bool QUAD_BATCH_FLUSH = GAME_PATCHER_QUAD_BATCH_FLUSH != 0;
}
