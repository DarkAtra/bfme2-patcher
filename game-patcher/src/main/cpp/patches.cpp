#include <windows.h>
#include <cstdint>

#include "ai_construction_site_production.h"
#include "ai_ability_conditions.h"
#include "collision_pair_table.h"
#include "pathfinder_cell_centre.h"
#include "collision_contact_handler.h"
#include "pathfinder_crowd_cost.h"
#include "render_lock.h"
#include "features.h"
#include "frame_limiter_sleep.h"
#include "patches.h"
#include "render_scope_names.h"
#include "health_bar_crash_fix.h"
#include "render_rate.h"
#include "sse_math.h"

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

    return count;
}
