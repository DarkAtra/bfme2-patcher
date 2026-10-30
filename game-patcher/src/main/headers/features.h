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

#ifndef GAME_PATCHER_RENDER_INTERPOLATION
#define GAME_PATCHER_RENDER_INTERPOLATION 1
#endif

#ifndef GAME_PATCHER_PRODUCTION_IDS
#define GAME_PATCHER_PRODUCTION_IDS 0
#endif

#ifndef GAME_PATCHER_AI_BUILD_QUEUE
#define GAME_PATCHER_AI_BUILD_QUEUE 1
#endif

#ifndef GAME_PATCHER_MOUNT_TOGGLE_CRASH_FIX
#define GAME_PATCHER_MOUNT_TOGGLE_CRASH_FIX 1
#endif

#ifndef GAME_PATCHER_EVACUATE_CONTAINED_HEROES
#define GAME_PATCHER_EVACUATE_CONTAINED_HEROES 1
#endif

#ifndef GAME_PATCHER_SIEGE_WALL_TELEPORT_FIX
#define GAME_PATCHER_SIEGE_WALL_TELEPORT_FIX 1
#endif

#ifndef GAME_PATCHER_AI_REBUILD_TARGETS
#define GAME_PATCHER_AI_REBUILD_TARGETS 1
#endif

#ifndef GAME_PATCHER_GROUP_ABILITY_CONDITIONS
#define GAME_PATCHER_GROUP_ABILITY_CONDITIONS 1
#endif

#ifndef GAME_PATCHER_PATHFINDER_CLOSED_GATES
#define GAME_PATCHER_PATHFINDER_CLOSED_GATES 1
#endif

#ifndef GAME_PATCHER_TOOLTIP_UPGRADE_DESCRIPTION
#define GAME_PATCHER_TOOLTIP_UPGRADE_DESCRIPTION 1
#endif

#ifndef GAME_PATCHER_TOOLTIP_KILL_COUNT
#define GAME_PATCHER_TOOLTIP_KILL_COUNT 1
#endif

#ifndef GAME_PATCHER_MOUNT_HEALTH_RATIO
#define GAME_PATCHER_MOUNT_HEALTH_RATIO 1
#endif

#ifndef GAME_PATCHER_HERO_RECRUIT_QUEUE
#define GAME_PATCHER_HERO_RECRUIT_QUEUE 1
#endif

#ifndef GAME_PATCHER_HERO_BATTALION_ABSORPTION
#define GAME_PATCHER_HERO_BATTALION_ABSORPTION 1
#endif

#ifndef GAME_PATCHER_CONSTRUCTION_INITIAL_HEALTH
#define GAME_PATCHER_CONSTRUCTION_INITIAL_HEALTH 1
#endif

#ifndef GAME_PATCHER_COOLDOWN_MODIFIERS
#define GAME_PATCHER_COOLDOWN_MODIFIERS 1
#endif

#ifndef GAME_PATCHER_GROUP_ABILITY_COOLDOWN
#define GAME_PATCHER_GROUP_ABILITY_COOLDOWN 1
#endif

#ifndef GAME_PATCHER_LIFE_DRAIN
#define GAME_PATCHER_LIFE_DRAIN 1
#endif

#ifndef GAME_PATCHER_TOOLTIP_TIMERS
#define GAME_PATCHER_TOOLTIP_TIMERS 1
#endif

#ifndef GAME_PATCHER_FPS_COUNTER
#define GAME_PATCHER_FPS_COUNTER 1
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

    // Render interpolation: moving units, turning and animations no longer take a double step once per logic frame; the
    // interpolation between two logic frames is spread evenly over the frames the client draws (render_interpolation.cpp).
    constexpr bool RENDER_INTERPOLATION = GAME_PATCHER_RENDER_INTERPOLATION != 0;

    // Game-wide production ids: production ids are counted for the whole game instead of per building, so recruiting a hero
    // at a second building while another hero is coming no longer takes the money and starts nothing (production_ids.cpp).
    // Off: in 2.02 heroes come only from fortress-type buildings, so it matters only when a player also owns a hero building
    // a map places; kept for later.
    constexpr bool PRODUCTION_IDS = GAME_PATCHER_PRODUCTION_IDS != 0;

    // AI build queue: a skirmish AI build order that cannot get a builder is skipped instead of stopping every order behind it,
    // so the AI no longer banks money with idle production buildings (ai_build_queue.cpp).
    constexpr bool AI_BUILD_QUEUE = GAME_PATCHER_AI_BUILD_QUEUE != 0;

    // Mount toggle crash fix: mounting, dismounting or changing form no longer crashes when the unit's last order was to move to
    // an object that is gone, e.g. the One Ring after it was picked up (mount_toggle_crash_fix.cpp).
    constexpr bool MOUNT_TOGGLE_CRASH_FIX = GAME_PATCHER_MOUNT_TOGGLE_CRASH_FIX != 0;

    // Evacuate contained heroes: the EVACUATE button of a garrison or tunnel also sends out the heroes that can carry the One
    // Ring, which it left inside (evacuate_contained_heroes.cpp).
    constexpr bool EVACUATE_CONTAINED_HEROES = GAME_PATCHER_EVACUATE_CONTAINED_HEROES != 0;

    // Siege wall teleport fix: a siege engine pushed against a wall is no longer lifted onto it; climbing a ramp onto the wall
    // still works (siege_wall_teleport_fix.cpp).
    constexpr bool SIEGE_WALL_TELEPORT_FIX = GAME_PATCHER_SIEGE_WALL_TELEPORT_FIX != 0;

    // AI rebuild targets: the skirmish AI no longer casts Rebuild (and Snowbind and Untamed Allegiance, which use the same
    // targeting) on buildings that are still under construction (ai_rebuild_targets.cpp).
    constexpr bool AI_REBUILD_TARGETS = GAME_PATCHER_AI_REBUILD_TARGETS != 0;

    // Group ability conditions: when a group casts an ability (OK_FOR_MULTI_EXECUTE), units whose own button is disabled by
    // their state are skipped instead of casting too (group_ability_conditions.cpp).
    constexpr bool GROUP_ABILITY_CONDITIONS = GAME_PATCHER_GROUP_ABILITY_CONDITIONS != 0;

    // Paths through closed gates: when a gate closes, units whose path runs through it plan a new one instead of walking
    // through the closed gate (pathfinder_closed_gates.cpp).
    constexpr bool PATHFINDER_CLOSED_GATES = GAME_PATCHER_PATHFINDER_CLOSED_GATES != 0;

    // Tooltip upgrade description: a researched upgrade's tooltip keeps its description, with "already researched" added below
    // it instead of replacing it (tooltip_upgrade_description.cpp).
    constexpr bool TOOLTIP_UPGRADE_DESCRIPTION = GAME_PATCHER_TOOLTIP_UPGRADE_DESCRIPTION != 0;

    // Tooltip kill count: a unit's hover tooltip shows its kills (a battalion member its battalion's), formatted from the mod's
    // TOOLTIP:Kills string; without that string nothing changes (tooltip_kill_count.cpp).
    constexpr bool TOOLTIP_KILL_COUNT = GAME_PATCHER_TOOLTIP_KILL_COUNT != 0;

    // Mount health ratio: mounting and dismounting hand the health to the other form as a fraction of the maximum health
    // instead of as hit points, so toggling no longer heals or hurts the unit (mount_health_ratio.cpp).
    constexpr bool MOUNT_HEALTH_RATIO = GAME_PATCHER_MOUNT_HEALTH_RATIO != 0;

    // Hero recruits and the queue: a hero being recruited no longer holds up the units and upgrades queued behind it; its
    // recruit time is unchanged (hero_recruit_queue.cpp).
    constexpr bool HERO_RECRUIT_QUEUE = GAME_PATCHER_HERO_RECRUIT_QUEUE != 0;

    // Heroes joining battalions: a hero who leaves a building while a battalion (e.g. Mordor's Black Riders) is walking out of
    // it no longer joins that battalion; only objects that fit one of its free slots do (hero_battalion_absorption.cpp).
    constexpr bool HERO_BATTALION_ABSORPTION = GAME_PATCHER_HERO_BATTALION_ABSORPTION != 0;

    // Construction initial health: a structure starts its construction at 10 % of its maximum health instead of 1 hit point;
    // health still reaches the maximum when construction completes (construction_initial_health.cpp).
    constexpr bool CONSTRUCTION_INITIAL_HEALTH = GAME_PATCHER_CONSTRUCTION_INITIAL_HEALTH != 0;

    // Cooldowns follow recharge modifiers: a recharge modifier gained or lost while an ability recharges changes the rest of
    // that cooldown instead of only the next one (cooldown_modifiers.cpp).
    constexpr bool COOLDOWN_MODIFIERS = GAME_PATCHER_COOLDOWN_MODIFIERS != 0;

    // Group ability cooldown: with several units selected, an ability the whole group may use (OK_FOR_MULTI_EXECUTE) stays
    // ready and clickable while any of them has it ready, and fires on the ready ones only (group_ability_cooldown.cpp).
    constexpr bool GROUP_ABILITY_COOLDOWN = GAME_PATCHER_GROUP_ABILITY_COOLDOWN != 0;

    // Life drain on every hit: a DrainLife weapon heals its owner when each hit lands, so delayed hits (DelayTime) and every tick
    // of damage over time drain too, not only the first tick (life_drain.cpp).
    constexpr bool LIFE_DRAIN = GAME_PATCHER_LIFE_DRAIN != 0;

    // Tooltip timers: a button's tooltip ends with its ability's cooldown (or the time left), its unit's build time or its upgrade's
    // research time, formatted from the mod's TOOLTIP:Cooldown, TOOLTIP:CooldownRemaining, TOOLTIP:BuildTime and
    // TOOLTIP:ResearchTime strings; without them nothing changes (tooltip_timers.cpp).
    constexpr bool TOOLTIP_TIMERS = GAME_PATCHER_TOOLTIP_TIMERS != 0;

    // FPS counter: with -showfps on the game's command line (the updater adds it in debug mode), the frames per second are shown
    // in the top left corner (fps_counter.cpp).
    constexpr bool FPS_COUNTER = GAME_PATCHER_FPS_COUNTER != 0;
}
