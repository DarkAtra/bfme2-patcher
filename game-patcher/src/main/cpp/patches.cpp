#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "cell_center.h"
#include "crowd_cost_memo.h"
#include "device_lock.h"
#include "features.h"
#include "pace_sleep.h"
#include "patches.h"
#include "pathfinder_timing.h"
#include "perf_scope_skip.h"
#include "render_rate.h"
#include "sse_rounding.h"
#include "stats.h"

namespace patching {

    namespace {

        Stats *statsBlock = nullptr;
    }

    Stats *createStats() {
        if (!features::STATS) {
            return nullptr;
        }
        if (statsBlock != nullptr) {
            return statsBlock;
        }

        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"Local\\GamePatcherStats");
        if (mapping == nullptr) {
            return nullptr;
        }
        void *view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
        if (view == nullptr) {
            return nullptr;
        }

        statsBlock = static_cast<Stats *>(view);
        memset(statsBlock, 0, sizeof(Stats));
        memcpy(statsBlock->magic, "GPSTATS1", sizeof(statsBlock->magic));
        statsBlock->control = 3;

        LARGE_INTEGER frequency, before, after;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&before);
        const uint64_t cyclesBefore = __rdtsc();
        Sleep(20);
        QueryPerformanceCounter(&after);
        const uint64_t cyclesAfter = __rdtsc();
        statsBlock->tscKHz = static_cast<uint32_t>((cyclesAfter - cyclesBefore) * frequency.QuadPart / ((after.QuadPart - before.QuadPart) * 1000));
        return statsBlock;
    }

    Stats *stats() {
        return statsBlock;
    }
}

size_t applyPatches(const wchar_t *(&failed)[MAX_FAILED_PATCHES]) {

    size_t count = 0;

    patching::createStats();

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
    if (features::STATS && !applyPathfinderTiming()) {
        failed[count++] = L"pathfinder timing";
    }

    return count;
}
