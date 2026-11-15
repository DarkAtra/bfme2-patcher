#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "command_line.h"
#include "d3d_methods.h"
#include "instrumentation/d3d_call_stats.h"
#include "instrumentation/stats.h"

// D3D call stats (debug mode with -d3dstats): counts every Direct3D 9 and D3DX effect call the game thread makes, by interface
// method, to tell how often a render thread like bfme2-accelerator's would have to wait for its queue (stats.h, D3dCallStats). The
// calls come from d3d_methods' hooks (every vtable class of the device, the effect and the resources, the D3DX functions the
// accelerator wraps through the exe's import slots); this is their client, so it refuses while the render thread is applied.
// Calls whose return address lies in d3d9.dll or d3dx9_27.dll are D3D9's and D3DX's own (D3DX sets the device up inside the effect
// calls) and are not counted. It costs some time per D3D call, so it is not part of the normal debug mode runs.
//
// With -d3dsim as well it simulates such a render thread (stats.h, RenderSim): a call the game makes during the engine update gets
// its return address replaced by a stub, so its duration is measured whatever its arguments; a call the render thread would queue
// is copied into a ring first, as its recording would copy it (arguments, the data they point to, a staged buffer lock's bytes).
// The simulated worker runs the queued calls with their measured durations; the simulated game thread runs its own work, the
// recording and the calls that stay on it, and waits where the accelerator waits. call_timing's own D3D wrappers are left out then,
// so that their time is not taken for D3D time.

using namespace instrumentation;
using d3d::kindOf;
using d3d::slotOf;

static_assert(D3D_STATS_METHODS == d3d::METHODS && D3D_STATS_D3DX == d3d::D3DX_FUNCTIONS, "the call stats count by d3d_methods' numbers");
static_assert(offsetof(D3dCallStats, recordings) == offsetof(D3dCallStats, vtables) + 4 && offsetof(D3dCallStats, refused) == offsetof(D3dCallStats, vtables) + 8,
              "D3dCallStats begins with d3d::HookCounts");

namespace {

    constexpr uint32_t NO_LOCK = 0xFFFFFFFF;

    // the hand-written methods that may wait in the accelerator: Reset, UpdateSurface, GetRenderTarget, GetDepthStencilSurface,
    // GetNPatchMode, the effect's description and handle getters (cached there), GetInt and SetStateManager
    bool waitsAtTimes(const uint32_t method) {
        return method == d3d::RESET || method == d3d::UPDATE_SURFACE || method == d3d::GET_RENDER_TARGET
            || method == d3d::GET_DEPTH_STENCIL_SURFACE || method == d3d::GET_NPATCH_MODE || (method >= 123 && method <= 139)
            || method == 147 || method == 191;
    }

    bool enabled = false;

    void noteCaller(D3dCallStats &counted, const uint32_t address, const uint32_t method) {
        for (D3dCaller &caller : counted.callers) {
            if (caller.address == address && caller.method == method) {
                caller.calls++;
                return;
            }
            if (caller.address == 0) {
                caller = {address, method, 1};
                return;
            }
        }
        counted.otherCallers++;
    }

    // D3DPOOL_SYSTEMMEM or D3DPOOL_SCRATCH, from a D3DSURFACE_DESC / D3DVOLUME_DESC (the pool is the fourth word of both)
    bool systemPool(const uint32_t (&desc)[8]) {
        return desc[3] == 2 || desc[3] == 3;
    }

    uint32_t bufferLock(const uint32_t flags, const uint32_t discard) {
        // D3DLOCK_READONLY, D3DLOCK_DISCARD, D3DLOCK_NOOVERWRITE
        if ((flags & 0x10) != 0) {
            return discard + 2;
        }
        if ((flags & 0x2000) != 0) {
            return discard;
        }
        if ((flags & 0x1000) != 0) {
            return discard + 1;
        }
        return discard + 3;
    }

    // frame: the return address, the object, then the arguments
    uint32_t lockKind(const uint32_t method, const uint32_t *frame) {
        uint32_t desc[8] = {};
        switch (method) {
            case d3d::VERTEX_BUFFER_LOCK:
                return bufferLock(frame[5], LOCK_VB_DISCARD);
            case d3d::INDEX_BUFFER_LOCK:
                return bufferLock(frame[5], LOCK_IB_DISCARD);
            case d3d::TEXTURE_LOCK: {
                // GetLevelDesc
                const HRESULT result = slotOf<HRESULT (__stdcall *)(uint32_t, uint32_t, uint32_t *)>(frame[1], 17)(frame[1], frame[2], desc);
                return SUCCEEDED(result) && systemPool(desc) ? LOCK_TEXTURE_SYSTEM : LOCK_TEXTURE_DRAWN;
            }
            case d3d::SURFACE_LOCK: {
                // GetDesc
                const HRESULT result = slotOf<HRESULT (__stdcall *)(uint32_t, uint32_t *)>(frame[1], 12)(frame[1], desc);
                return SUCCEEDED(result) && systemPool(desc) ? LOCK_SURFACE_SYSTEM : LOCK_SURFACE_DRAWN;
            }
            case d3d::CUBE_LOCK:
            case d3d::VOLUME_TEXTURE_LOCK:
            case d3d::VOLUME_LOCK:
                return LOCK_CUBE_OR_VOLUME;
            default:
                return NO_LOCK;
        }
    }

    // ---- the render thread simulation (-d3dsim)

    // a number no interface uses: the calibration's stand-in method
    constexpr uint32_t CALIBRATION = D3D_STATS_METHODS - 1;
    // SurfaceClass::DrawPixel 0x005165E0 and its alpha twin 0x005166C0: the accelerator turns their locks into queued writes
    constexpr d3d::Range PIXEL_WRITES = {0x005165E0, 0x00516770};

    constexpr size_t RING_BYTES = 16 * 1024 * 1024;
    constexpr size_t SCRATCH_BYTES = 1024 * 1024;
    BYTE *ring = nullptr;
    BYTE *scratch = nullptr;
    size_t ringAt = 0;
    volatile LONG references = 0;

    uint32_t primitiveVertices(const uint32_t type, const uint32_t primitives) {
        switch (type) {
            case 1:
                return primitives;
            case 2:
                return primitives * 2;
            case 3:
                return primitives + 1;
            case 4:
                return primitives * 3;
            case 5:
            case 6:
                return primitives + 2;
            default:
                return 0;
        }
    }

    // what a queued call carries besides its arguments and their data: a staged buffer lock's bytes (the accelerator copies them at
    // the unlock), a user pointer draw's vertices and indices
    size_t extraBytes(const uint32_t method, const uint32_t *frame) {
        switch (method) {
            case d3d::VERTEX_BUFFER_LOCK:
            case d3d::INDEX_BUFFER_LOCK: {
                if (frame[3] != 0) {
                    return frame[3];
                }
                // GetDesc: format, type, usage, pool, size (and the vertex format)
                uint32_t desc[6] = {};
                if (SUCCEEDED(slotOf<HRESULT (__stdcall *)(uint32_t, uint32_t *)>(frame[1], 13)(frame[1], desc)) && desc[4] > frame[2]) {
                    return desc[4] - frame[2];
                }
                return 0;
            }
            case d3d::DRAW_PRIMITIVE_UP:
                return static_cast<size_t>(primitiveVertices(frame[2], frame[3])) * frame[5];
            case d3d::DRAW_INDEXED_PRIMITIVE_UP:
                // D3DFMT_INDEX16
                return static_cast<size_t>(primitiveVertices(frame[2], frame[5])) * (frame[7] == 101 ? 2 : 4)
                    + static_cast<size_t>(frame[4]) * frame[9];
            default:
                return 0;
        }
    }

    // copies what the accelerator's recording copies of a queued call into the ring: the method, the object, the arguments, the
    // data they point to and `extra` bytes; takes a reference to every COM object argument the way it does (here on a stand-in)
    uint32_t recordCall(const uint32_t method, const uint32_t *frame, size_t extra) {
        const d3d::Layout &layout = d3d::layoutOf(method);
        size_t sizes[2] = {};
        size_t total = 8 + 4 * static_cast<size_t>(layout.arguments);
        __try {
            for (size_t b = 0; b < layout.blocks; b++) {
                const d3d::Block &block = layout.block[b];
                if (frame[2 + block.pointer] != 0) {
                    sizes[b] = (std::min<size_t>)(SCRATCH_BYTES, static_cast<size_t>(block.bytes) * (block.count == d3d::NO_COUNT ? 1 : frame[2 + block.count]));
                }
                total += sizes[b];
            }
            const char *text = layout.string != 0 ? reinterpret_cast<const char *>(frame[2 + layout.string - 1]) : nullptr;
            const size_t textBytes = text != nullptr ? strnlen(text, 4096) + 1 : 0;
            extra = (std::min)(extra, SCRATCH_BYTES);
            total += textBytes + extra;
            if (ringAt + total > RING_BYTES) {
                ringAt = 0;
            }
            BYTE *at = ring + ringAt;
            memcpy(at, &method, 4);
            memcpy(at + 4, &frame[1], 4);
            memcpy(at + 8, frame + 2, 4 * static_cast<size_t>(layout.arguments));
            at += 8 + 4 * static_cast<size_t>(layout.arguments);
            for (size_t b = 0; b < layout.blocks; b++) {
                memcpy(at, reinterpret_cast<const void *>(frame[2 + layout.block[b].pointer]), sizes[b]);
                at += sizes[b];
            }
            memcpy(at, text, textBytes);
            at += textBytes;
            memcpy(at, scratch, extra);
            at += extra;
            for (uint32_t argument = 0; argument < layout.arguments; argument++) {
                if ((layout.objects >> argument & 1) != 0 && frame[2 + argument] != 0) {
                    _InterlockedIncrement(&references);
                }
            }
            ringAt = (static_cast<size_t>(at - ring) + 15) & ~static_cast<size_t>(15);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
        return static_cast<uint32_t>(total);
    }

    enum Treatment : uint32_t {
        // recorded, run by the worker
        QUEUED,
        // run on the game thread without waiting (direct, under the queue's lock, from a mirror or a cache)
        DIRECT,
        // the game thread waits until the worker has run everything queued, then runs it
        WAITS,
        // queued, once the previous frame's Present has run
        PRESENTS,
    };

    // the object whose lock waited or ran directly: its unlock runs directly as well
    uint32_t heldLock = 0;

    uint32_t treatmentOf(const uint32_t method, const uint32_t *frame, const uint32_t lock) {
        if (method >= D3D_STATS_METHODS) {
            return WAITS;
        }
        if (method == d3d::PRESENT || method == d3d::SWAP_CHAIN_PRESENT) {
            return PRESENTS;
        }
        switch (lock) {
            case LOCK_VB_READONLY:
            case LOCK_IB_READONLY:
                return WAITS;
            case LOCK_VB_DISCARD:
            case LOCK_VB_NOOVERWRITE:
            case LOCK_VB_PLAIN:
            case LOCK_IB_DISCARD:
            case LOCK_IB_NOOVERWRITE:
            case LOCK_IB_PLAIN:
                return QUEUED;
            case LOCK_TEXTURE_DRAWN:
            case LOCK_SURFACE_DRAWN:
                if (PIXEL_WRITES.contains(frame[0])) {
                    return QUEUED;
                }
                heldLock = frame[1];
                return WAITS;
            case LOCK_TEXTURE_SYSTEM:
            case LOCK_SURFACE_SYSTEM:
                heldLock = frame[1];
                return DIRECT;
            default:
                break;
        }
        if ((method == d3d::TEXTURE_UNLOCK || method == d3d::SURFACE_UNLOCK) && frame[1] == heldLock) {
            heldLock = 0;
            return DIRECT;
        }
        switch (kindOf(method)) {
            case 'S':
                return WAITS;
            case 'D':
            case 'L':
            case '?':
                return DIRECT;
            case 'X':
                // Reset and GetNPatchMode wait; the buffer and texture creations run directly; GetRenderTarget and
                // GetDepthStencilSurface come from mirrors, the effect's description and handle getters, GetInt and IsParameterUsed
                // from caches
                if (method == d3d::RESET || method == d3d::GET_NPATCH_MODE) {
                    return WAITS;
                }
                if (method == 23 || method == 26 || method == 27 || method == d3d::GET_RENDER_TARGET
                    || method == d3d::GET_DEPTH_STENCIL_SURFACE || (method >= 123 && method <= 139) || method == 147 || method == 182) {
                    return DIRECT;
                }
                return QUEUED;
            default:
                return QUEUED;
        }
    }

    // The two timelines of a frame, in cycles from its start: the game thread's clock, when the worker has run everything queued
    // so far, and when it has run the last Present.
    struct SimClock {
        int64_t game;
        int64_t worker;
        int64_t present;
    };

    // Advances the timelines by one call that took `duration` cycles; returns the cycles the game thread waited.
    int64_t simStep(SimClock &clock, const uint32_t treatment, const int64_t duration) {
        int64_t waited = 0;
        switch (treatment) {
            case QUEUED:
                clock.worker = (std::max)(clock.worker, clock.game) + duration;
                break;
            case DIRECT:
                clock.game += duration;
                break;
            case WAITS:
                waited = (std::max<int64_t>)(0, clock.worker - clock.game);
                clock.game += waited + duration;
                clock.worker = clock.game;
                break;
            case PRESENTS:
                waited = (std::max<int64_t>)(0, clock.present - clock.game);
                clock.game += waited;
                clock.worker = (std::max)(clock.worker, clock.game) + duration;
                clock.present = clock.worker;
                break;
            default:
                break;
        }
        return waited;
    }

    struct Pending {
        uint32_t returnAddress;
        uint32_t method;
        uint32_t treatment;
        uint64_t recorded;
    };

    constexpr size_t PENDING_DEPTH = 16;
    Pending pending[PENDING_DEPTH];
    size_t pendingDepth = 0;
    bool simulating = false;
    bool inFrame = false;
    uint64_t frameStart = 0;
    uint64_t lastReal = 0;
    SimClock clock = {};
    uint32_t returnStub = 0;
    // the instrumentation's own cycles inside a timed call and inside an empty recording (calibrate)
    int64_t bracket = 0;
    int64_t recordBracket = 0;
    int64_t lastDuration = 0;
    int64_t lastRecord = 0;

    // A call of the game, before it runs: the game thread's clock moves on by the work since the last call and by the recording,
    // and the call returns into returnStub.
    void beginTimed(const uint32_t method, uint32_t *frame, const uint32_t lock) {
        RenderSim &sim = stats()->renderSim;
        const uint32_t treatment = treatmentOf(method, frame, lock);
        const size_t extra = treatment == QUEUED ? extraBytes(method, frame) : 0;
        const uint64_t entry = __rdtsc();
        clock.game += static_cast<int64_t>(entry - lastReal);
        if (treatment == QUEUED || treatment == PRESENTS) {
            sim.bytes += recordCall(method, frame, extra);
        }
        const uint64_t recorded = __rdtsc();
        lastRecord = static_cast<int64_t>(recorded - entry);
        const int64_t recording = (std::max<int64_t>)(0, lastRecord - recordBracket);
        clock.game += recording;
        sim.record += static_cast<uint64_t>(recording);
        pending[pendingDepth++] = {frame[0], method, treatment, recorded};
        frame[0] = returnStub;
    }

    // The call has returned (into returnStub); returns where it was to return to.
    uint32_t __cdecl noteReturn() {
        const uint64_t exit = __rdtsc();
        const Pending call = pending[--pendingDepth];
        lastDuration = static_cast<int64_t>(exit - call.recorded);
        const int64_t duration = (std::max<int64_t>)(0, lastDuration - bracket);
        RenderSim &sim = stats()->renderSim;
        const int64_t waited = simStep(clock, call.treatment, duration);
        sim.calls++;
        if (call.treatment == QUEUED || call.treatment == PRESENTS) {
            sim.queued += static_cast<uint64_t>(duration);
        } else {
            sim.direct += static_cast<uint64_t>(duration);
        }
        if (call.treatment == PRESENTS) {
            if (waited > 0) {
                sim.presentWaits++;
                sim.presentWaited += static_cast<uint64_t>(waited);
            }
        } else if (call.treatment == WAITS) {
            sim.waits++;
            sim.waited += static_cast<uint64_t>(waited);
            if (call.method < D3D_STATS_METHODS + D3D_STATS_D3DX) {
                sim.waitedBy[call.method] += static_cast<uint64_t>(waited);
            }
        }
        lastReal = exit;
        return call.returnAddress;
    }

    uint32_t __cdecl noteCall(const uint32_t method, uint32_t *frame, uint32_t) {
        Stats *statistics = stats();
        if (statistics == nullptr) {
            return 0;
        }
        D3dCallStats &counted = statistics->d3dCallStats;
        if (__readfsdword(0x24) != d3d::gameThread()) {
            InterlockedIncrement(reinterpret_cast<volatile LONG *>(&counted.otherThreadCalls));
            return 0;
        }
        const uint32_t from = frame[0];
        const bool own = method < D3D_STATS_METHODS && d3d::fromD3d(from);
        uint32_t lock = NO_LOCK;
        if (method >= D3D_STATS_METHODS) {
            counted.d3dx[method - D3D_STATS_METHODS]++;
            noteCaller(counted, from, method);
        } else if (!own) {
            counted.calls[method]++;
            lock = lockKind(method, frame);
            if (lock != NO_LOCK) {
                counted.locks[lock]++;
            }
            const char kind = kindOf(method);
            if (lock != NO_LOCK || kind == 'S' || kind == 'L' || waitsAtTimes(method)) {
                noteCaller(counted, from, method);
            }
        }
        if (simulating && inFrame && !own && pendingDepth < PENDING_DEPTH) {
            beginTimed(method, frame, lock);
        }
        return 0;
    }

    constexpr size_t RETURN_STUB_BYTES = 32;

    // sub esp, 4 / push eax / push ecx / push edx / call noteReturn / mov [esp + 12], eax / pop edx / pop ecx / pop eax / ret: the
    // timed call's result (eax, edx; a float stays on the x87 stack) goes back to where the call was to return to
    uint32_t emitReturnStub() {
        BYTE code[RETURN_STUB_BYTES] = {
            0x83, 0xEC, 0x04, 0x50, 0x51, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x0C, 0x5A, 0x59, 0x58, 0xC3,
            0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
        };
        BYTE *at = d3d::codeSpace(sizeof(code));
        if (at == nullptr) {
            return 0;
        }
        const auto call = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&noteReturn) - (reinterpret_cast<uintptr_t>(at) + 11));
        memcpy(code + 7, &call, sizeof(call));
        memcpy(at, code, sizeof(code));
        return reinterpret_cast<uint32_t>(at);
    }

    int __stdcall calibrationTarget(const int value) {
        return value + 1;
    }

    uint32_t calibrationOriginal = 0;

    // The cycles the timing itself adds to a call (the thunk, the hooks' dispatch, noteCall's tail, the stub) and to a recording: the
    // median of many timed calls of an empty function, made the way the game's calls are timed.
    bool calibrate() {
        constexpr size_t SAMPLES = 4096;
        static int64_t durations[SAMPLES];
        static int64_t recordings[SAMPLES];
        calibrationOriginal = reinterpret_cast<uint32_t>(&calibrationTarget);
        const auto target = reinterpret_cast<int (__stdcall *)(int)>(d3d::thunkFor(CALIBRATION, &calibrationOriginal));
        if (target == nullptr) {
            return false;
        }
        inFrame = true;
        lastReal = __rdtsc();
        volatile int sink = 0;
        for (size_t i = 0; i < SAMPLES; i++) {
            sink = target(sink);
            durations[i] = lastDuration;
            recordings[i] = lastRecord;
        }
        inFrame = false;
        std::nth_element(durations, durations + SAMPLES / 2, durations + SAMPLES);
        std::nth_element(recordings, recordings + SAMPLES / 2, recordings + SAMPLES);
        bracket = durations[SAMPLES / 2];
        recordBracket = recordings[SAMPLES / 2];
        Stats *statistics = stats();
        statistics->d3dCallStats.calls[CALIBRATION] = 0;
        memset(&statistics->renderSim, 0, sizeof(statistics->renderSim));
        statistics->renderSim.bracket = static_cast<uint32_t>(bracket);
        statistics->renderSim.recordBracket = static_cast<uint32_t>(recordBracket);
        clock = {};
        return true;
    }
}

bool instrumentation::applyD3dCallStats() {
    Stats *statistics = stats();
    if (statistics == nullptr || enabled) {
        return false;
    }
    if (!d3d::start(&noteCall, reinterpret_cast<d3d::HookCounts *>(&statistics->d3dCallStats.vtables))) {
        return false;
    }
    enabled = true;
    if (hasArgument(L"-d3dsim")) {
        ring = static_cast<BYTE *>(VirtualAlloc(nullptr, RING_BYTES + SCRATCH_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (ring == nullptr) {
            return false;
        }
        scratch = ring + RING_BYTES;
        returnStub = emitReturnStub();
        if (returnStub == 0) {
            return false;
        }
        simulating = true;
        if (!calibrate()) {
            simulating = false;
            return false;
        }
    }
    return enabled;
}

void instrumentation::d3dCallStatsDevice(const uint32_t device) {
    if (enabled) {
        d3d::hookDevice(device);
    }
}

void instrumentation::d3dCallStatsEffect(const uint32_t vtable) {
    if (enabled) {
        d3d::hookEffect(vtable);
    }
}

bool instrumentation::d3dCallStatsSimulates() {
    return simulating;
}

void instrumentation::d3dCallStatsFrame(const bool begin) {
    Stats *statistics = stats();
    if (!simulating || statistics == nullptr || __readfsdword(0x24) != d3d::gameThread()) {
        return;
    }
    const uint64_t now = __rdtsc();
    if (begin) {
        inFrame = true;
        frameStart = now;
        lastReal = now;
        clock.game = 0;
        return;
    }
    if (!inFrame) {
        return;
    }
    inFrame = false;
    clock.game += static_cast<int64_t>(now - lastReal);
    RenderSim &sim = statistics->renderSim;
    const uint64_t busyCycles = now - frameStart;
    sim.frames++;
    sim.busy += busyCycles;
    sim.simulated += static_cast<uint64_t>(clock.game);
    const uint64_t perMs = statistics->tscKHz != 0 ? statistics->tscKHz : 1;
    sim.realHistogram[(std::min<uint64_t>)(SIM_BUCKETS - 1, busyCycles / perMs)]++;
    sim.simulatedHistogram[(std::min<uint64_t>)(SIM_BUCKETS - 1, static_cast<uint64_t>(clock.game) / perMs)]++;
    // the next frame starts where this one ended on the game thread; what the worker still has to run carries over
    clock.worker = (std::max<int64_t>)(0, clock.worker - clock.game);
    clock.present = (std::max<int64_t>)(0, clock.present - clock.game);
    clock.game = 0;
}
