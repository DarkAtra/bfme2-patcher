#pragma once

#include <windows.h>
#include <cstdint>

// The queue between the game thread and the render thread's worker (render_thread.cpp): one producer, one consumer, a ring of
// records the worker runs in the order they were committed.
namespace render_queue {

    struct Record {
        // with this header, a multiple of 16
        uint32_t bytes;
        uint32_t tag;
    };

    // runs a record on the worker (the payload follows the header); true ends the batch after it, so that the producer sees it
    // run at once
    using Runner = bool (*)(const Record *record);

    // ringBytes: a power of two of at least 64 KB. The worker thread is started, not waited for (the dll is applied under the
    // loader lock). `between` (optional) runs on the worker after each batch, outside the exec lock.
    bool start(Runner runner, uint32_t ringBytes, void (*between)() = nullptr);

    // drains, ends the worker and frees the ring
    void stop();

    // The producer's side, from one thread. reserve gives the space of a record with `payload` bytes after its header, waiting while
    // the ring is full; nullptr for a record larger than half the ring (commit then does nothing). The record runs once committed;
    // one record is reserved at a time.
    Record *reserve(uint32_t tag, uint32_t payload);
    void commit();

    // Waits until the worker has run everything committed. Sent window messages are handled while waiting once the worker has stood
    // still for 100 ms: the driver may send the game's window messages from the worker (Present, Reset) and wait for them.
    void drain();

    // the producer's position after the records committed so far; waitUntil waits (as drain does) until the worker has run
    // everything before it and tells whether it had to wait
    uint32_t position();
    bool waitUntil(uint32_t position);

    // From any thread: waits until the worker has run everything committed, without events or message handling. For a thread other
    // than the producer, at a time the producer records nothing (the load screen's thread holds the renderer's lock then).
    void waitIdle();

    bool idle();

    // the worker holds it while it runs a batch of records; a thread that calls the device directly takes it
    void lock();
    void unlock();

    uint32_t workerThread();

    // with GAME_PATCHER_RENDER_THREAD_COUNTS (features.h), otherwise 0: time stamp counter ticks / 1024 the worker has spent running
    // records, and the producer (waitTicks only from it) waiting for the worker
    uint32_t busyTicks();
    uint32_t waitTicks();
}
