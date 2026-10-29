#include <windows.h>
#include <cstdint>

#include "ai_construction_gate.h"
#include "ai_power_model_condition.h"
#include "broad_phase_table.h"
#include "cell_center.h"
#include "contact_scan.h"
#include "crowd_cost_memo.h"
#include "device_lock.h"
#include "features.h"
#include "pace_sleep.h"
#include "patches.h"
#include "perf_scope_skip.h"
#include "quad_batch_cap.h"
#include "render_rate.h"
#include "sse_rounding.h"

size_t applyPatches(const wchar_t *(&failed)[MAX_FAILED_PATCHES]) {

    size_t count = 0;

    if (features::RENDER_RATE && !applyRenderRatePatch()) {
        failed[count++] = L"60 fps";
    }
    if (features::PACE_SLEEP && !applyPaceSleep()) {
        failed[count++] = L"frame limiter sleep";
    }
    if (features::PERF_SCOPE_SKIP && !applyPerfScopeSkip()) {
        failed[count++] = L"render scope names";
    }
    if (features::DEVICE_LOCK && !applyDeviceLock()) {
        failed[count++] = L"render lock";
    }
    if (features::SSE_ROUNDING && !applySseRounding()) {
        failed[count++] = L"floor and ceil";
    }
    if (features::CELL_CENTER && !applyCellCenter()) {
        failed[count++] = L"pathfinder cell centre";
    }
    if (features::CROWD_COST_MEMO && !applyCrowdCostMemo()) {
        failed[count++] = L"pathfinder crowd cost";
    }
    if (features::BROAD_PHASE_TABLE && !applyBroadPhaseTable()) {
        failed[count++] = L"broad phase pair table";
    }
    if (features::CONTACT_SCAN && !applyContactScan()) {
        failed[count++] = L"contact handler";
    }
    if (features::QUAD_BATCH_CAP && !applyQuadBatchCap()) {
        failed[count++] = L"overlay batch cap";
    }
    if (features::AI_CONSTRUCTION_GATE && !applyAiConstructionGate()) {
        failed[count++] = L"AI construction gate";
    }
    if (features::AI_POWER_MODEL_CONDITION && !applyAiPowerModelCondition()) {
        failed[count++] = L"AI power button conditions";
    }

    return count;
}
