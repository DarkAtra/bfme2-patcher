#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "instrumentation/call_timing.h"
#include "instrumentation/full_memory_dump.h"
#include "instrumentation/instrumentation.h"
#include "instrumentation/network_stats.h"
#include "instrumentation/pathfinder_timing.h"
#include "instrumentation/stats.h"
#include "patches.h"

// The measuring tools, applied after the patches when the game is started with -debugmode (the updater adds it in debug mode).
// They record what the game does (the timers in a file mapping that the harness reads, the network stats in files) and change
// nothing about how it runs; their hooks sit on functions and call sites the patches leave alone. Without -debugmode none of
// this is applied.

namespace {

    instrumentation::Stats *statsBlock = nullptr;

    bool applyStats() {
        return instrumentation::createStats() != nullptr;
    }
}

instrumentation::Stats *instrumentation::createStats() {
    if (statsBlock != nullptr) {
        return statsBlock;
    }

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, STATS_BYTES, L"Local\\GamePatcherStats");
    if (mapping == nullptr) {
        return nullptr;
    }
    // another game on this machine (or the offline test) owns the block: leave it to that one, this game runs without the timers
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mapping);
        return nullptr;
    }
    void *view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, STATS_BYTES);
    if (view == nullptr) {
        CloseHandle(mapping);
        return nullptr;
    }

    statsBlock = static_cast<Stats *>(view);
    memset(statsBlock, 0, sizeof(Stats));
    memcpy(statsBlock->magic, "GPSTATS1", sizeof(statsBlock->magic));

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

instrumentation::Stats *instrumentation::stats() {
    return statsBlock;
}

void instrumentation::apply(const wchar_t *(&failed)[MAX_FAILED_PATCHES], size_t &count) {
    runPatch(true, L"Statistics block", applyStats, failed, count);
    const bool timers = stats() != nullptr;
    runPatch(timers, L"Scope timers", applyPathfinderTiming, failed, count);
    runPatch(timers, L"Call timers", applyCallTiming, failed, count);
    runPatch(true, L"Network stats", applyNetworkStats, failed, count);
    runPatch(fullMemoryDumpRequested(), L"Full memory dump", applyFullMemoryDump, failed, count);
}
