#include <windows.h>
#include <cstdint>

#include "ai_construction_site_production.h"
#include "ai_ability_conditions.h"
#include "ai_build_queue.h"
#include "ai_rebuild_targets.h"
#include "ai_tunnel_routing.h"
#include "collision_pair_table.h"
#include "command_line.h"
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
#include "instrumentation/instrumentation.h"
#include "life_drain.h"
#include "material_parameters.h"
#include "mount_health_ratio.h"
#include "siege_wall_teleport_fix.h"
#include "mount_toggle_crash_fix.h"
#include "network_catch_up.h"
#include "network_command_send.h"
#include "os_name_fallback.h"
#include "render_rate.h"
#include "sse_math.h"
#include "tooltip_kill_count.h"
#include "tooltip_timers.h"
#include "tooltip_upgrade_description.h"

void runPatch(const bool enabled, const wchar_t *name, bool (*apply)(), const wchar_t *(&failed)[MAX_FAILED_PATCHES], size_t &count) {
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

size_t applyPatches(const wchar_t *(&failed)[MAX_FAILED_PATCHES]) {

    size_t count = 0;

    patching::records().patchCount = 0;
    patching::records().caveCount = 0;

    runPatch(features::CRASH_DUMP, L"Crash dumps", applyCrashDump, failed, count);
    runPatch(features::RENDER_RATE, L"60 fps", applyRenderRatePatch, failed, count);
    runPatch(features::FRAME_LIMITER_SLEEP, L"Frame limiter sleep", applyFrameLimiterSleep, failed, count);
    runPatch(features::RENDER_SCOPE_NAMES, L"Render scope names", applyRenderScopeNames, failed, count);
    runPatch(features::RENDER_LOCK, L"Render lock", applyRenderLock, failed, count);
    runPatch(features::SSE_MATH, L"SSE math functions", applySseMath, failed, count);
    runPatch(features::PATHFINDER_CELL_CENTRE, L"Pathfinder cell centre", applyPathfinderCellCentre, failed, count);
    runPatch(features::PATHFINDER_CROWD_COST_MEMO, L"Pathfinder crowd cost", applyPathfinderCrowdCost, failed, count);
    runPatch(features::COLLISION_PAIR_TABLE, L"Collision pair table", applyCollisionPairTable, failed, count);
    runPatch(features::COLLISION_CONTACT_HANDLER, L"Collision contact handler", applyCollisionContactHandler, failed, count);
    runPatch(features::HEALTH_BAR_CRASH_FIX, L"Health bar crash fix", applyHealthBarCrashFix, failed, count);
    runPatch(features::AI_CONSTRUCTION_SITE_PRODUCTION, L"AI production from construction sites", applyAiConstructionSiteProduction, failed, count);
    runPatch(features::AI_ABILITY_CONDITIONS, L"AI ability conditions", applyAiAbilityConditions, failed, count);
    runPatch(features::PRODUCTION_IDS, L"Game-wide production ids", applyProductionIds, failed, count);
    runPatch(features::AI_BUILD_QUEUE, L"AI build queue", applyAiBuildQueue, failed, count);
    runPatch(features::MOUNT_TOGGLE_CRASH_FIX, L"Mount toggle crash fix", applyMountToggleCrashFix, failed, count);
    runPatch(features::EVACUATE_CONTAINED_HEROES, L"Evacuate contained heroes", applyEvacuateContainedHeroes, failed, count);
    runPatch(features::SIEGE_WALL_TELEPORT_FIX, L"Siege wall teleport fix", applySiegeWallTeleportFix, failed, count);
    runPatch(features::AI_REBUILD_TARGETS, L"AI rebuild targets", applyAiRebuildTargets, failed, count);
    runPatch(features::GROUP_ABILITY_CONDITIONS, L"Group ability conditions", applyGroupAbilityConditions, failed, count);
    runPatch(features::PATHFINDER_CLOSED_GATES, L"Paths through closed gates", applyPathfinderClosedGates, failed, count);
    runPatch(features::TOOLTIP_UPGRADE_DESCRIPTION, L"Tooltip upgrade description", applyTooltipUpgradeDescription, failed, count);
    runPatch(features::TOOLTIP_KILL_COUNT, L"Tooltip kill count", applyTooltipKillCount, failed, count);
    runPatch(features::MOUNT_HEALTH_RATIO, L"Mount health ratio", applyMountHealthRatio, failed, count);
    runPatch(features::HERO_RECRUIT_QUEUE, L"Hero recruits and the queue", applyHeroRecruitQueue, failed, count);
    runPatch(features::HERO_BATTALION_ABSORPTION, L"Heroes joining battalions", applyHeroBattalionAbsorption, failed, count);
    runPatch(features::CONSTRUCTION_INITIAL_HEALTH, L"Construction initial health", applyConstructionInitialHealth, failed, count);
    runPatch(features::COOLDOWN_MODIFIERS, L"Cooldowns follow recharge modifiers", applyCooldownModifiers, failed, count);
    runPatch(features::GROUP_ABILITY_COOLDOWN, L"Group ability cooldown", applyGroupAbilityCooldown, failed, count);
    runPatch(features::LIFE_DRAIN, L"Life drain on every hit", applyLifeDrain, failed, count);
    runPatch(features::TOOLTIP_TIMERS, L"Tooltip timers", applyTooltipTimers, failed, count);
    runPatch(features::NETWORK_CATCH_UP, L"Network catch-up", applyNetworkCatchUp, failed, count);
    runPatch(features::NETWORK_COMMAND_SEND, L"Network command send", applyNetworkCommandSend, failed, count);
    runPatch(features::MATERIAL_PARAMETERS, L"Material parameters", applyMaterialParameters, failed, count);
    runPatch(features::HEAT_HAZE_SQUARES, L"Heat haze squares", applyHeatHazeSquares, failed, count);
    runPatch(features::AI_TUNNEL_ROUTING, L"AI tunnel routing", applyAiTunnelRouting, failed, count);
    runPatch(features::OS_NAME_FALLBACK, L"OS name fallback", applyOsNameFallback, failed, count);
    runPatch(features::FPS_COUNTER && hasArgument(L"-showfps"), L"FPS counter", applyFpsCounter, failed, count);

    if (hasArgument(L"-debugmode")) {
        instrumentation::apply(failed, count);
    }

    return count;
}
