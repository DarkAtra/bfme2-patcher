#pragma once

#include <cstddef>
#include <cstdint>

#include "render_recorder.h"

namespace instrumentation {

    // Inclusive time in functions the game calls (pathfinder_timing.cpp). Scopes 9 to 15 are the phases 0 to 6 of the
    // per-frame update at 0x0062E4E8 (phase 0 never runs).
    constexpr size_t SCOPE_COUNT = 40;
    constexpr size_t ADJUST_SUBSCOPES = 7;
    constexpr size_t ADJUST_RING = 2048;
    constexpr size_t MESSAGE_RING = 4096;
    constexpr size_t STATS_BYTES = 512 * 1024;

    // What the two pair tables of the collision broad phase (0x00B6E6B0) held after one update, sampled every few frames
    // (pathfinder_timing.cpp): the tables are chained hash tables, the histograms count buckets by chain length (0, 1, 2, 3 to 4,
    // 5 to 8, 9 to 16, 17 to 32, more).
    constexpr size_t BROAD_PHASE_RING = 128;
    constexpr size_t BROAD_PHASE_BUCKETS = 8;

    struct BroadPhaseSample {
        uint32_t call;
        uint32_t proxies;
        uint32_t axes;
        uint32_t nodesA;
        uint32_t nodesB;
        uint32_t maxChainA;
        uint32_t maxChainB;
        uint32_t stepsA;
        uint32_t stepsB;
        uint32_t overlapping;
        uint32_t histogramA[BROAD_PHASE_BUCKETS];
        uint32_t histogramB[BROAD_PHASE_BUCKETS];
        // the pairs of the second table by their state word at +0x10: 0 not decided yet, 1 collides, 2 ignored
        uint32_t statesB[3];
    };

    struct ScopeStat {
        uint32_t calls;
        uint32_t cyclesLow;
        uint32_t cyclesHigh;
        uint32_t maxCycles;
    };

    // One call of AdjustDestination: where it was called from, its time, whether it had a group destination (the group's path
    // tightening runs then), how many cell checks, path existence queries and relationship queries it made, and the time in the
    // scopes 1 to 5, 19 and 18 inside it.
    struct AdjustRecord {
        uint32_t caller;
        uint32_t cycles;
        uint32_t groupDestination;
        uint32_t cellChecks;
        uint32_t pathExistCalls;
        uint32_t relationCalls;
        uint32_t sub[ADJUST_SUBSCOPES];
    };

    // Inclusive time of the calls call_timing.cpp times, one entry per call site and per vtable (virtual calls) or target (direct
    // calls), or per render scope (the game's PerfScope objects, keyed by the place that opens them). `child` is the time spent in
    // timed calls nested inside, so inclusive minus child is the entry's own time. Entries are added in the order they are first
    // seen; `count` is written after the entry.
    constexpr size_t CALL_ENTRIES = 2048;
    constexpr size_t CALL_NAME = 40;

    enum CallGroup : uint32_t {
        CALL_ENGINE = 0,
        CALL_LOGIC = 1,
        CALL_MODULE = 2,
        CALL_CLIENT = 3,
        CALL_RENDER_SCOPE = 4,
        CALL_RESOURCE = 5,
    };

    struct CallStat {
        uint32_t site;
        uint32_t key;
        uint32_t target;
        uint32_t group;
        uint32_t calls;
        uint32_t cyclesLow;
        uint32_t cyclesHigh;
        uint32_t childLow;
        uint32_t childHigh;
        uint32_t maxCycles;
        char name[CALL_NAME];
    };

    struct CallTimes {
        uint32_t count;
        // calls that found the table full or the nesting too deep, and therefore were not timed
        uint32_t lost;
        CallStat entries[CALL_ENTRIES];
    };

    // Every Present that took 30 ms or more since the dll was loaded (the last PRESENT_STALLS of them): when it ended, in time stamp
    // counter ticks since the load, and how long it took.
    constexpr size_t PRESENT_STALLS = 256;

    struct PresentStall {
        uint32_t endLow;
        uint32_t endHigh;
        uint32_t cycles;
    };

    struct PresentStalls {
        uint32_t count;
        PresentStall stalls[PRESENT_STALLS];
    };

    // The D3DX effect parameter setters (ID3DXEffect vtable, hooked by call_timing.cpp when the game first commits an effect):
    // calls on the game thread, how many set the value the same effect parameter already had from the setter before, and the time
    // spent in them.
    constexpr size_t EFFECT_SETTERS = 10;

    struct EffectSetter {
        uint32_t calls;
        uint32_t same;
        uint32_t cyclesLow;
        uint32_t cyclesHigh;
    };

    struct EffectStats {
        uint32_t hookedVtable;
        EffectSetter setters[EFFECT_SETTERS];
    };

    // The Direct3D device's shader and shader constant writes on the game thread (SetTexture, SetVertexShader,
    // SetVertexShaderConstantF/I/B, SetPixelShader, SetPixelShaderConstantF/I/B), by where they came from: outside any D3DX effect
    // call, or inside the effect's Begin, BeginPass, CommitChanges, EndPass or End. The callers of the writes from outside are kept
    // by return address (the first DEVICE_CALLERS of them, the rest are counted in otherCallers).
    constexpr size_t DEVICE_WRITES = 9;
    constexpr size_t WRITE_CONTEXTS = 6;
    constexpr size_t DEVICE_CALLERS = 32;

    struct DeviceCaller {
        uint32_t address;
        uint32_t write;
        uint32_t calls;
    };

    struct DeviceWrites {
        uint32_t calls[DEVICE_WRITES][WRITE_CONTEXTS];
        DeviceCaller callers[DEVICE_CALLERS];
        uint32_t otherCallers;
    };

    // The Direct3D calls the game makes on the game thread (d3d_call_stats.cpp, only with -d3dstats), to tell how often a render thread
    // like bfme2-accelerator's would have to wait for its queue. Calls D3D9 and D3DX make themselves are not counted. `calls` is per
    // interface method, numbered as the accelerator numbers them (device 0, effect 120, texture 200, cube texture 230, volume texture
    // 260, surface 290, volume 310, vertex buffer 330, index buffer 350, state block 370, query 380, swap chain 390, vertex shader 400,
    // pixel shader 410, declaration 420, each plus the vtable slot); `d3dx` per D3DX function the accelerator wraps; `locks` by kind
    // (D3dLockKind; DRAWN: a default or managed pool resource). The callers are kept for the calls that wait or lock in the
    // accelerator (its kinds S and L, the locks, the D3DX functions and a few hand-written ones), the first D3D_STATS_CALLERS of them;
    // method is D3D_STATS_METHODS + n for D3DX function n.
    constexpr size_t D3D_STATS_METHODS = 432;
    constexpr size_t D3D_STATS_D3DX = 14;
    constexpr size_t D3D_STATS_LOCKS = 16;
    constexpr size_t D3D_STATS_CALLERS = 128;

    enum D3dLockKind : uint32_t {
        LOCK_VB_DISCARD, LOCK_VB_NOOVERWRITE, LOCK_VB_READONLY, LOCK_VB_PLAIN,
        LOCK_IB_DISCARD, LOCK_IB_NOOVERWRITE, LOCK_IB_READONLY, LOCK_IB_PLAIN,
        LOCK_TEXTURE_DRAWN, LOCK_TEXTURE_SYSTEM, LOCK_SURFACE_DRAWN, LOCK_SURFACE_SYSTEM, LOCK_CUBE_OR_VOLUME,
    };

    struct D3dCaller {
        uint32_t address;
        uint32_t method;
        uint32_t calls;
    };

    struct D3dCallStats {
        uint32_t vtables;
        // state block recordings begun (BeginStateBlock calls that succeeded)
        uint32_t recordings;
        // vtables not hooked: a slot outside D3D9 / D3DX / this dll, or no room left
        uint32_t refused;
        uint32_t otherThreadCalls;
        uint32_t calls[D3D_STATS_METHODS];
        uint32_t d3dx[D3D_STATS_D3DX];
        uint32_t locks[D3D_STATS_LOCKS];
        D3dCaller callers[D3D_STATS_CALLERS];
        uint32_t otherCallers;
    };

    // The render thread simulation (d3d_call_stats.cpp, with -d3dsim besides -d3dstats): per engine update (one frame, 0x00639EDC) it
    // times every D3D call the game thread makes, copies each call that a render thread like bfme2-accelerator's would queue into a
    // ring as such a thread records it, and runs a simulated worker that executes the queued calls one after another with the
    // measured durations. The simulated game thread does its own work as measured, records, and waits where that render thread
    // would wait (its kind S calls, READONLY buffer locks, locks of default or managed textures and surfaces outside the engine's
    // per-pixel writes, the D3DX functions it runs on the game thread) and at Present while the previous frame's Present has not
    // run (one frame in flight). The time between engine updates (the frame limiter) is left out, as on a CPU that never waits.
    // All times are time stamp counter cycles; `bracket` is the instrumentation's own share of a timed call, already subtracted.
    constexpr size_t SIM_BUCKETS = 32;

    struct RenderSim {
        uint32_t frames;
        uint32_t calls;
        uint32_t waits;
        uint32_t presentWaits;
        uint32_t bracket;
        uint32_t recordBracket;
        uint64_t busy;
        uint64_t simulated;
        uint64_t queued;
        uint64_t direct;
        uint64_t record;
        uint64_t waited;
        uint64_t presentWaited;
        uint64_t bytes;
        // frames by their busy time, real and simulated: bucket n holds n to n + 1 ms, the last one everything longer
        uint32_t realHistogram[SIM_BUCKETS];
        uint32_t simulatedHistogram[SIM_BUCKETS];
        // cycles waited at each method (d3d_methods' numbering, D3DX functions behind)
        uint64_t waitedBy[D3D_STATS_METHODS + D3D_STATS_D3DX];
    };

    // One call of the game logic's message dispatcher (0x00779A3D), which turns a player's order into the unit commands that
    // call AdjustDestination.
    struct MessageRecord {
        uint32_t type;
        uint32_t cycles;
        uint32_t adjustCycles;
        uint32_t adjustCalls;
    };

    // What the instrumentation records in debug mode. It lives in a file mapping called "Local\GamePatcherStats", so a measuring
    // tool can open it without knowing where the game put it.
    struct Stats {
        char magic[8];

        // Time stamp counter ticks per millisecond, to turn the cycle counts into time.
        uint32_t tscKHz;

        ScopeStat scopes[SCOPE_COUNT];
        uint32_t adjustRecordWrites;
        AdjustRecord adjustRecords[ADJUST_RING];
        uint32_t messageRecordWrites;
        MessageRecord messageRecords[MESSAGE_RING];

        uint32_t broadPhaseSampleWrites;
        BroadPhaseSample broadPhaseSamples[BROAD_PHASE_RING];

        CallTimes callTimes;

        PresentStalls presentStalls;

        EffectStats effectStats;

        DeviceWrites deviceWrites;

        D3dCallStats d3dCallStats;

        RenderSim renderSim;

        // the render thread's counters (render_recorder.h), copied at the end of every engine update while it is applied
        render_recorder::Counts renderThread;
    };

    static_assert(sizeof(Stats) <= STATS_BYTES, "the statistics block does not fit its file mapping");

    // Creates the mapping. Returns null if the mapping could not be created.
    Stats *createStats();

    // The block created by createStats, or null.
    Stats *stats();
}
