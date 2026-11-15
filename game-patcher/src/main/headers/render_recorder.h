#pragma once

#include <cstdint>

#include "d3d_methods.h"

// The render thread's recording (being built, tools/analysis/notes/20-accelerator-port.md): the client of d3d_methods' hooks that
// turns the game thread's Direct3D calls into records of render_queue, which its worker runs in the same order.
namespace render_recorder {

    constexpr size_t WAIT_CALLERS = 64;

    struct WaitCaller {
        // the return address of the game's call
        uint32_t address;
        uint32_t method;
        uint32_t waits;
    };

    // kept only in a build with GAME_PATCHER_RENDER_THREAD_COUNTS (features.h); otherwise all zero but hooks
    struct Counts {
        d3d::HookCounts hooks;
        // calls recorded (a recorded Release counts twice)
        uint32_t recorded;
        // calls that waited until the worker had run everything recorded, then ran on the game thread
        uint32_t waited;
        // calls that ran on the game thread at once (kind D)
        uint32_t direct;
        // calls that ran on the game thread under the exec lock, between the worker's batches (kind L)
        uint32_t locked;
        // Releases that waited: records still held references to the object, so the count to hand back was not known
        uint32_t releasesWaited;
        // records the worker ran, as of the last counts()
        volatile long ran;
        // calls of other threads than the game thread and the worker: they wait until the worker has run everything recorded, then
        // run under the exec lock
        volatile long otherThreads;
        // state block recordings begun on the worker (D3DX's, inside a recorded Begin or BeginPass)
        volatile long workerRecordings;
        // the method the worker ran when it began the last of them
        uint32_t workerRecordingMethod;
        // devices made after the first: the recording stops for good at the first (only the first device is hooked)
        uint32_t deviceChanges;
        // records that handed the game thread's x87 control word and MXCSR to the worker
        uint32_t stateRecords;
        // Presents after which the game thread waited for the previous frame's Present to run
        uint32_t presentWaits;
        // vertex / index buffer and surface locks handed a mirror instead of waiting
        uint32_t staged;
        // the waits by method (d3d_methods' numbers, the D3DX functions behind)
        uint32_t waitsBy[d3d::METHODS + d3d::D3DX_FUNCTIONS];
        // the callers of the waits (when the table is full, a new place takes the row with the fewest, counting on from there, so the
        // frequent ones stay), and how often a row changed hands
        WaitCaller waitCallers[WAIT_CALLERS];
        uint32_t otherWaitCallers;
        // surfaces refused a mirror for what they are, the last of them (format, usage, pool, width, height), and mirrors not made
        // for want of room (a later lock tries again)
        uint32_t unsuitedSurfaces;
        uint32_t lastUnsuitedSurface[5];
        uint32_t surfaceMirrorsWithoutRoom;
        // the last surface lock that waited because the surface has no mirror yet: format, usage, pool, width, height, its locks so far
        uint32_t lastCountedSurface[6];
        // time stamp counter ticks / 1024, as of the last counts(): the worker running records, the game thread waiting for it
        uint32_t workerTicks;
        uint32_t waitTicks;
    };

    // on the game thread: the hooks' client and the worker
    bool start();

    // the device once it exists; recording begins with it
    void hookDevice(uint32_t device);
    void hookEffect(uint32_t vtable);

    const Counts &counts();
}
