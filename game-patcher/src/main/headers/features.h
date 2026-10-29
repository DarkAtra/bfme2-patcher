#pragma once

// Compile time switches for the patches game-patcher applies to the game process. Each one can be turned off on its own
// by setting it to 0 here or on the compiler command line (e.g. /DGAME_PATCHER_RENDER_LOCK=0). A disabled patch is not
// applied at all, so the game keeps its stock code for it.

#ifndef GAME_PATCHER_RENDER_RATE
#define GAME_PATCHER_RENDER_RATE 1
#endif

#ifndef GAME_PATCHER_FRAME_LIMITER_SLEEP
#define GAME_PATCHER_FRAME_LIMITER_SLEEP 1
#endif

#ifndef GAME_PATCHER_RENDER_SCOPE_NAMES
#define GAME_PATCHER_RENDER_SCOPE_NAMES 1
#endif

#ifndef GAME_PATCHER_RENDER_LOCK
#define GAME_PATCHER_RENDER_LOCK 1
#endif

#ifndef GAME_PATCHER_SSE_MATH
#define GAME_PATCHER_SSE_MATH 1
#endif

#ifndef GAME_PATCHER_PATHFINDER_CELL_CENTRE
#define GAME_PATCHER_PATHFINDER_CELL_CENTRE 1
#endif

#ifndef GAME_PATCHER_PATHFINDER_CROWD_COST_MEMO
#define GAME_PATCHER_PATHFINDER_CROWD_COST_MEMO 1
#endif

#ifndef GAME_PATCHER_PATHFINDER_CROWD_COST_REWRITE
#define GAME_PATCHER_PATHFINDER_CROWD_COST_REWRITE 1
#endif

#ifndef GAME_PATCHER_COLLISION_PAIR_TABLE
#define GAME_PATCHER_COLLISION_PAIR_TABLE 1
#endif

#ifndef GAME_PATCHER_COLLISION_CONTACT_HANDLER
#define GAME_PATCHER_COLLISION_CONTACT_HANDLER 1
#endif

#ifndef GAME_PATCHER_HEALTH_BAR_CRASH_FIX
#define GAME_PATCHER_HEALTH_BAR_CRASH_FIX 1
#endif

#ifndef GAME_PATCHER_HEALTH_BAR_CRASH_FIX_FLUSH
#define GAME_PATCHER_HEALTH_BAR_CRASH_FIX_FLUSH 1
#endif

#ifndef GAME_PATCHER_AI_CONSTRUCTION_SITE_PRODUCTION
#define GAME_PATCHER_AI_CONSTRUCTION_SITE_PRODUCTION 1
#endif

#ifndef GAME_PATCHER_AI_ABILITY_CONDITIONS
#define GAME_PATCHER_AI_ABILITY_CONDITIONS 1
#endif

namespace features {

    // 60 fps: draws 60 frames per second at the stock game pace and keeps what the client counts in frames (particles, camera
    // scrolling and rotation, the camera reset click) at its speed at 30 fps (render_rate.cpp).
    constexpr bool RENDER_RATE = GAME_PATCHER_RENDER_RATE != 0;

    // Frame limiter sleep: the limiter sleeps through the rest of a frame instead of spinning on Sleep(0), so the main thread
    // no longer pins a core (frame_limiter_sleep.cpp).
    constexpr bool FRAME_LIMITER_SLEEP = GAME_PATCHER_FRAME_LIMITER_SLEEP != 0;

    // Render scope names: skips building the profiler event names of the render scopes, which nothing reads
    // (render_scope_names.cpp).
    constexpr bool RENDER_SCOPE_NAMES = GAME_PATCHER_RENDER_SCOPE_NAMES != 0;

    // Render lock: a user mode lock instead of the kernel mutex around every render call (render_lock.cpp).
    constexpr bool RENDER_LOCK = GAME_PATCHER_RENDER_LOCK != 0;

    // SSE math functions: floor, ceil, sqrt and fabs without the x87 control word switches of msvcr71 (sse_math.cpp).
    constexpr bool SSE_MATH = GAME_PATCHER_SSE_MATH != 0;

    // Pathfinder cell centre: the crowd cost computes the cell centre once per call instead of once per unit
    // (pathfinder_cell_centre.cpp).
    constexpr bool PATHFINDER_CELL_CENTRE = GAME_PATCHER_PATHFINDER_CELL_CENTRE != 0;

    // Pathfinder crowd cost: remembers the answers about each unit for the duration of one pathfinder search
    // (pathfinder_crowd_cost.cpp).
    constexpr bool PATHFINDER_CROWD_COST_MEMO = GAME_PATCHER_PATHFINDER_CROWD_COST_MEMO != 0;

    // Pathfinder crowd cost, with PATHFINDER_CROWD_COST_MEMO: computes the crowd cost from one cached record per unit and
    // search instead of asking the game about every unit in every cell window (pathfinder_crowd_cost.cpp).
    constexpr bool PATHFINDER_CROWD_COST_REWRITE = GAME_PATCHER_PATHFINDER_CROWD_COST_REWRITE != 0;

    // Collision pair table: the collision broad phase gets a pair table of 262139 buckets instead of 11131, so its lookups no
    // longer walk long chains with a big army on the map (collision_pair_table.cpp). The game behaves exactly as before.
    constexpr bool COLLISION_PAIR_TABLE = GAME_PATCHER_COLLISION_PAIR_TABLE != 0;

    // Collision contact handler: skips the modules whose collide interface getter always returns null instead of calling
    // them (collision_contact_handler.cpp). The game behaves exactly as before.
    constexpr bool COLLISION_CONTACT_HANDLER = GAME_PATCHER_COLLISION_CONTACT_HANDLER != 0;

    // Health bar crash fix: stops the 2D overlay batch (health bars and the like) from taking more indices than its 16 bit
    // counters hold, which crashes the stock game with a big army on screen (health_bar_crash_fix.cpp).
    constexpr bool HEALTH_BAR_CRASH_FIX = GAME_PATCHER_HEALTH_BAR_CRASH_FIX != 0;

    // Health bar crash fix, with HEALTH_BAR_CRASH_FIX: draws the full batch and starts a new one, so every health bar stays
    // visible. Without it the rectangles beyond the limit are not drawn (health_bar_crash_fix.cpp).
    constexpr bool HEALTH_BAR_CRASH_FIX_FLUSH = GAME_PATCHER_HEALTH_BAR_CRASH_FIX_FLUSH != 0;

    // AI production from construction sites: the skirmish AI no longer produces units or revives heroes in buildings that are
    // still under construction, which the player cannot do either (ai_construction_site_production.cpp).
    constexpr bool AI_CONSTRUCTION_SITE_PRODUCTION = GAME_PATCHER_AI_CONSTRUCTION_SITE_PRODUCTION != 0;

    // AI ability conditions: the AI no longer casts abilities whose button the state of the unit disables, which the player
    // cannot press either, e.g. a silenced hero's abilities (ai_ability_conditions.cpp).
    constexpr bool AI_ABILITY_CONDITIONS = GAME_PATCHER_AI_ABILITY_CONDITIONS != 0;
}
