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
#include "cooldown_modifiers.h"
#include "evacuate_contained_heroes.h"
#include "group_ability_conditions.h"
#include "group_ability_cooldown.h"
#include "pathfinder_crowd_cost.h"
#include "render_lock.h"
#include "features.h"
#include "frame_limiter_sleep.h"
#include "patches.h"
#include "production_ids.h"
#include "render_scope_names.h"
#include "health_bar_crash_fix.h"
#include "hero_battalion_absorption.h"
#include "hero_recruit_queue.h"
#include "life_drain.h"
#include "mount_health_ratio.h"
#include "siege_wall_teleport_fix.h"
#include "mount_toggle_crash_fix.h"
#include "render_interpolation.h"
#include "render_rate.h"
#include "sse_math.h"
#include "tooltip_kill_count.h"
#include "tooltip_timers.h"
#include "tooltip_upgrade_description.h"

size_t applyPatches(const wchar_t *(&failed)[MAX_FAILED_PATCHES]) {

    size_t count = 0;

    if (features::RENDER_RATE && !applyRenderRatePatch()) {
        failed[count++] = L"60 fps";
    }
    if (features::FRAME_LIMITER_SLEEP && !applyFrameLimiterSleep()) {
        failed[count++] = L"Frame limiter sleep";
    }
    if (features::RENDER_SCOPE_NAMES && !applyRenderScopeNames()) {
        failed[count++] = L"Render scope names";
    }
    if (features::RENDER_LOCK && !applyRenderLock()) {
        failed[count++] = L"Render lock";
    }
    if (features::SSE_MATH && !applySseMath()) {
        failed[count++] = L"SSE math functions";
    }
    if (features::PATHFINDER_CELL_CENTRE && !applyPathfinderCellCentre()) {
        failed[count++] = L"Pathfinder cell centre";
    }
    if (features::PATHFINDER_CROWD_COST_MEMO && !applyPathfinderCrowdCost()) {
        failed[count++] = L"Pathfinder crowd cost";
    }
    if (features::COLLISION_PAIR_TABLE && !applyCollisionPairTable()) {
        failed[count++] = L"Collision pair table";
    }
    if (features::COLLISION_CONTACT_HANDLER && !applyCollisionContactHandler()) {
        failed[count++] = L"Collision contact handler";
    }
    if (features::HEALTH_BAR_CRASH_FIX && !applyHealthBarCrashFix()) {
        failed[count++] = L"Health bar crash fix";
    }
    if (features::AI_CONSTRUCTION_SITE_PRODUCTION && !applyAiConstructionSiteProduction()) {
        failed[count++] = L"AI production from construction sites";
    }
    if (features::AI_ABILITY_CONDITIONS && !applyAiAbilityConditions()) {
        failed[count++] = L"AI ability conditions";
    }
    if (features::RENDER_INTERPOLATION && !applyRenderInterpolation()) {
        failed[count++] = L"Render interpolation";
    }
    if (features::PRODUCTION_IDS && !applyProductionIds()) {
        failed[count++] = L"Game-wide production ids";
    }
    if (features::AI_BUILD_QUEUE && !applyAiBuildQueue()) {
        failed[count++] = L"AI build queue";
    }
    if (features::MOUNT_TOGGLE_CRASH_FIX && !applyMountToggleCrashFix()) {
        failed[count++] = L"Mount toggle crash fix";
    }
    if (features::EVACUATE_CONTAINED_HEROES && !applyEvacuateContainedHeroes()) {
        failed[count++] = L"Evacuate contained heroes";
    }
    if (features::SIEGE_WALL_TELEPORT_FIX && !applySiegeWallTeleportFix()) {
        failed[count++] = L"Siege wall teleport fix";
    }
    if (features::AI_REBUILD_TARGETS && !applyAiRebuildTargets()) {
        failed[count++] = L"AI rebuild targets";
    }
    if (features::GROUP_ABILITY_CONDITIONS && !applyGroupAbilityConditions()) {
        failed[count++] = L"Group ability conditions";
    }
    if (features::PATHFINDER_CLOSED_GATES && !applyPathfinderClosedGates()) {
        failed[count++] = L"Paths through closed gates";
    }
    if (features::TOOLTIP_UPGRADE_DESCRIPTION && !applyTooltipUpgradeDescription()) {
        failed[count++] = L"Tooltip upgrade description";
    }
    if (features::TOOLTIP_KILL_COUNT && !applyTooltipKillCount()) {
        failed[count++] = L"Tooltip kill count";
    }
    if (features::MOUNT_HEALTH_RATIO && !applyMountHealthRatio()) {
        failed[count++] = L"Mount health ratio";
    }
    if (features::HERO_RECRUIT_QUEUE && !applyHeroRecruitQueue()) {
        failed[count++] = L"Hero recruits and the queue";
    }
    if (features::HERO_BATTALION_ABSORPTION && !applyHeroBattalionAbsorption()) {
        failed[count++] = L"Heroes joining battalions";
    }
    if (features::CONSTRUCTION_INITIAL_HEALTH && !applyConstructionInitialHealth()) {
        failed[count++] = L"Construction initial health";
    }
    if (features::COOLDOWN_MODIFIERS && !applyCooldownModifiers()) {
        failed[count++] = L"Cooldowns follow recharge modifiers";
    }
    if (features::GROUP_ABILITY_COOLDOWN && !applyGroupAbilityCooldown()) {
        failed[count++] = L"Group ability cooldown";
    }
    if (features::LIFE_DRAIN && !applyLifeDrain()) {
        failed[count++] = L"Life drain on every hit";
    }
    if (features::TOOLTIP_TIMERS && !applyTooltipTimers()) {
        failed[count++] = L"Tooltip timers";
    }

    return count;
}
