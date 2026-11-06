#include <windows.h>
#include <cstdint>

#include "ai_construction_site_production.h"
#include "ai_ability_conditions.h"
#include "ai_build_queue.h"
#include "ai_rebuild_targets.h"
#include "collision_pair_table.h"
#include "pathfinder_cell_centre.h"
#include "pathfinder_closed_gates.h"
#include "collision_contact_handler.h"
#include "construction_initial_health.h"
#include "crash_dump.h"
#include "cooldown_modifiers.h"
#include "evacuate_contained_heroes.h"
#include "group_ability_conditions.h"
#include "group_ability_cooldown.h"
#include "pathfinder_crowd_cost.h"
#include "render_lock.h"
#include "features.h"
#include "fps_counter.h"
#include "frame_limiter_sleep.h"
#include "patch_set.h"
#include "patches.h"
#include "production_ids.h"
#include "render_scope_names.h"
#include "health_bar_crash_fix.h"
#include "heat_haze_squares.h"
#include "hero_battalion_absorption.h"
#include "hero_recruit_queue.h"
#include "life_drain.h"
#include "material_parameters.h"
#include "mount_health_ratio.h"
#include "siege_wall_teleport_fix.h"
#include "mount_toggle_crash_fix.h"
#include "network_catch_up.h"
#include "network_command_send.h"
#include "network_stats.h"
#include "render_interpolation.h"
#include "render_rate.h"
#include "sse_math.h"
#include "tooltip_kill_count.h"
#include "tooltip_timers.h"
#include "tooltip_upgrade_description.h"

namespace {

    void run(const bool enabled, const wchar_t *name, bool (*apply)(), const wchar_t *(&failed)[MAX_FAILED_PATCHES], size_t &count) {
        if (!enabled) {
            return;
        }
        patching::Records &recorded = patching::records();
        recorded.current = name;
        const bool applied = apply();
        recorded.current = nullptr;
        if (recorded.patchCount < patching::MAX_RECORDS) {
            recorded.patches[recorded.patchCount++] = {name, applied};
        }
        if (!applied && count < MAX_FAILED_PATCHES) {
            failed[count++] = name;
        }
    }
}

size_t applyPatches(const wchar_t *(&failed)[MAX_FAILED_PATCHES]) {

    size_t count = 0;

    patching::records().patchCount = 0;
    patching::records().caveCount = 0;

    run(features::CRASH_DUMP, L"Crash dumps", applyCrashDump, failed, count);
    run(features::RENDER_RATE, L"60 fps", applyRenderRatePatch, failed, count);
    run(features::FRAME_LIMITER_SLEEP, L"Frame limiter sleep", applyFrameLimiterSleep, failed, count);
    run(features::RENDER_SCOPE_NAMES, L"Render scope names", applyRenderScopeNames, failed, count);
    run(features::RENDER_LOCK, L"Render lock", applyRenderLock, failed, count);
    run(features::SSE_MATH, L"SSE math functions", applySseMath, failed, count);
    run(features::PATHFINDER_CELL_CENTRE, L"Pathfinder cell centre", applyPathfinderCellCentre, failed, count);
    run(features::PATHFINDER_CROWD_COST_MEMO, L"Pathfinder crowd cost", applyPathfinderCrowdCost, failed, count);
    run(features::COLLISION_PAIR_TABLE, L"Collision pair table", applyCollisionPairTable, failed, count);
    run(features::COLLISION_CONTACT_HANDLER, L"Collision contact handler", applyCollisionContactHandler, failed, count);
    run(features::HEALTH_BAR_CRASH_FIX, L"Health bar crash fix", applyHealthBarCrashFix, failed, count);
    run(features::AI_CONSTRUCTION_SITE_PRODUCTION, L"AI production from construction sites", applyAiConstructionSiteProduction, failed, count);
    run(features::AI_ABILITY_CONDITIONS, L"AI ability conditions", applyAiAbilityConditions, failed, count);
    run(features::RENDER_INTERPOLATION, L"Render interpolation", applyRenderInterpolation, failed, count);
    run(features::PRODUCTION_IDS, L"Game-wide production ids", applyProductionIds, failed, count);
    run(features::AI_BUILD_QUEUE, L"AI build queue", applyAiBuildQueue, failed, count);
    run(features::MOUNT_TOGGLE_CRASH_FIX, L"Mount toggle crash fix", applyMountToggleCrashFix, failed, count);
    run(features::EVACUATE_CONTAINED_HEROES, L"Evacuate contained heroes", applyEvacuateContainedHeroes, failed, count);
    run(features::SIEGE_WALL_TELEPORT_FIX, L"Siege wall teleport fix", applySiegeWallTeleportFix, failed, count);
    run(features::AI_REBUILD_TARGETS, L"AI rebuild targets", applyAiRebuildTargets, failed, count);
    run(features::GROUP_ABILITY_CONDITIONS, L"Group ability conditions", applyGroupAbilityConditions, failed, count);
    run(features::PATHFINDER_CLOSED_GATES, L"Paths through closed gates", applyPathfinderClosedGates, failed, count);
    run(features::TOOLTIP_UPGRADE_DESCRIPTION, L"Tooltip upgrade description", applyTooltipUpgradeDescription, failed, count);
    run(features::TOOLTIP_KILL_COUNT, L"Tooltip kill count", applyTooltipKillCount, failed, count);
    run(features::MOUNT_HEALTH_RATIO, L"Mount health ratio", applyMountHealthRatio, failed, count);
    run(features::HERO_RECRUIT_QUEUE, L"Hero recruits and the queue", applyHeroRecruitQueue, failed, count);
    run(features::HERO_BATTALION_ABSORPTION, L"Heroes joining battalions", applyHeroBattalionAbsorption, failed, count);
    run(features::CONSTRUCTION_INITIAL_HEALTH, L"Construction initial health", applyConstructionInitialHealth, failed, count);
    run(features::COOLDOWN_MODIFIERS, L"Cooldowns follow recharge modifiers", applyCooldownModifiers, failed, count);
    run(features::GROUP_ABILITY_COOLDOWN, L"Group ability cooldown", applyGroupAbilityCooldown, failed, count);
    run(features::LIFE_DRAIN, L"Life drain on every hit", applyLifeDrain, failed, count);
    run(features::TOOLTIP_TIMERS, L"Tooltip timers", applyTooltipTimers, failed, count);
    run(features::NETWORK_STATS, L"Network stats", applyNetworkStats, failed, count);
    run(features::NETWORK_CATCH_UP, L"Network catch-up", applyNetworkCatchUp, failed, count);
    run(features::NETWORK_COMMAND_SEND, L"Network command send", applyNetworkCommandSend, failed, count);
    run(features::MATERIAL_PARAMETERS, L"Material parameters", applyMaterialParameters, failed, count);
    run(features::HEAT_HAZE_SQUARES, L"Heat haze squares", applyHeatHazeSquares, failed, count);
    run(features::FPS_COUNTER && fpsCounterRequested(), L"FPS counter", applyFpsCounter, failed, count);

    return count;
}
