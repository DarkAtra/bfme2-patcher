#include <windows.h>
#include <intrin.h>
#include <cstdint>

#include "features.h"
#include "render_queue.h"

// Positions are byte counters that only grow (modulo 2^32, so the ring's size must divide it): the producer publishes `head` after
// writing a record, the worker `tail` after running a batch. A record never wraps: one that would not fit before the ring's end is
// preceded by a filler record that runs nothing. Both sides spin a little before they sleep on an event. The producer publishes
// `head` with a plain store (x86 keeps stores in order, and a volatile store keeps the compiler's): a locked exchange per record
// would wait for the record's bytes to leave the store buffer. So the worker can miss a commit made while it goes to sleep: while
// it sleeps it looks at `head` again every WAKE_MS, and the producer's next commit or wait wakes it anyway. The waits on the
// producer's side publish with a locked exchange before they look whether the worker sleeps.

namespace render_queue {

    namespace {

        constexpr bool counting = features::RENDER_THREAD_COUNTS;
        constexpr uint32_t FILLER = 0xFFFFFFFF;
        constexpr uint32_t HEADER = sizeof(Record);
        constexpr uint32_t BATCH = 256;
        constexpr int SPINS = 4000;
        // pauses between the worker's looks at head while it has caught up: each look takes the line away from the producer, which
        // writes it per record, and so would the worker's reads of the records it has just written
        constexpr int POLL = 16;
        constexpr DWORD WAKE_MS = 1;

        // a variable on a cache line of its own: the producer writes head per record, the worker finished per record and tail per
        // batch
        struct alignas(64) Line {
            volatile LONG value;
        };

        Line lines[5] = {};

        BYTE *ring = nullptr;
        uint32_t size = 0;
        Runner run = nullptr;
        void (*betweenBatches)() = nullptr;
        volatile LONG &head = lines[0].value;
        volatile LONG &tail = lines[1].value;
        volatile LONG &sleeping = lines[2].value;
        volatile LONG &waiting = lines[3].value;
        volatile LONG waitTarget = 0;
        volatile LONG quit = 0;
        // records the worker has finished, to tell a worker that stands still
        volatile LONG &finished = lines[4].value;
        constexpr DWORD STUCK_MS = 100;
        HANDLE wake = nullptr;
        HANDLE progress = nullptr;
        HANDLE thread = nullptr;
        DWORD workerId = 0;
        CRITICAL_SECTION exec;
        // the producer's own position: head plus a filler not yet committed
        alignas(64) uint32_t writeAt = 0;
        // tail as the producer saw it last: the ring has at least this much room
        uint32_t knownTail = 0;
        Record *current = nullptr;
        // time stamp counter ticks: the worker's in its batches (and / 1024 for other threads), the producer's in waitFor
        uint64_t busy = 0;
        volatile LONG busyShown = 0;
        uint64_t waited = 0;

        uint32_t load(const volatile LONG &value) {
            return static_cast<uint32_t>(value);
        }

        bool reached(const uint32_t position, const uint32_t target) {
            return static_cast<int32_t>(position - target) >= 0;
        }

        void wakeWorker() {
            if (sleeping != 0) {
                SetEvent(wake);
            }
        }

        DWORD WINAPI work(void *) {
            uint32_t at = load(tail);
            bool caughtUp = false;
            for (;;) {
                if (caughtUp) {
                    for (int i = 0; i < POLL; i++) {
                        _mm_pause();
                    }
                }
                uint32_t until = load(head);
                for (int spin = 0; until == at && spin < SPINS && quit == 0; spin++) {
                    _mm_pause();
                    if (spin % POLL == POLL - 1) {
                        until = load(head);
                    }
                }
                if (until == at) {
                    if (quit != 0) {
                        return 0;
                    }
                    InterlockedExchange(&sleeping, 1);
                    while (load(head) == at && quit == 0) {
                        WaitForSingleObject(wake, WAKE_MS);
                    }
                    InterlockedExchange(&sleeping, 0);
                    caughtUp = false;
                    continue;
                }
                EnterCriticalSection(&exec);
                const uint64_t began = counting ? __rdtsc() : 0;
                for (uint32_t n = 0; n < BATCH && at != until; n++) {
                    const auto *record = reinterpret_cast<const Record *>(ring + (at & (size - 1)));
                    const bool last = record->tag != FILLER && run(record);
                    finished = finished + 1;
                    at += record->bytes;
                    if (last) {
                        break;
                    }
                }
                if (counting) {
                    busy += __rdtsc() - began;
                    busyShown = static_cast<LONG>(busy >> 10);
                }
                LeaveCriticalSection(&exec);
                caughtUp = at == until;
                InterlockedExchange(&tail, static_cast<LONG>(at));
                if (waiting != 0 && reached(at, load(waitTarget))) {
                    SetEvent(progress);
                }
                if (betweenBatches != nullptr) {
                    betweenBatches();
                }
            }
        }

        // Until the worker has run everything before `target` (committed). Sent window messages are only handled once the worker has
        // finished no record for STUCK_MS: it may then wait for this thread to answer one (the driver inside Present or Reset).
        // Before that they wait, as they would while this thread were inside the call itself: a window procedure run here would
        // call Direct3D in the middle of the queue (the hooks run this thread's calls directly while it waits).
        void waitOn(const uint32_t target) {
            for (int spin = 0; spin < SPINS; spin++) {
                if (reached(load(tail), target)) {
                    return;
                }
                _mm_pause();
            }
            InterlockedExchange(&waitTarget, static_cast<LONG>(target));
            InterlockedExchange(&waiting, 1);
            LONG seen = finished;
            ULONGLONG since = GetTickCount64();
            bool stuck = false;
            while (!reached(load(tail), target)) {
                wakeWorker();
                if (!stuck) {
                    WaitForSingleObject(progress, STUCK_MS);
                    const ULONGLONG now = GetTickCount64();
                    if (finished != seen) {
                        seen = finished;
                        since = now;
                    } else if (now - since >= STUCK_MS) {
                        stuck = true;
                    }
                } else if (MsgWaitForMultipleObjectsEx(1, &progress, STUCK_MS, QS_SENDMESSAGE, 0) == WAIT_OBJECT_0 + 1) {
                    MSG message;
                    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
                }
            }
            InterlockedExchange(&waiting, 0);
        }

        void waitFor(const uint32_t target) {
            if (counting) {
                const uint64_t began = __rdtsc();
                waitOn(target);
                waited += __rdtsc() - began;
            } else {
                waitOn(target);
            }
        }
    }

    bool start(const Runner runner, const uint32_t ringBytes, void (*const between)()) {
        if (ring != nullptr || runner == nullptr || ringBytes < 64 * 1024 || (ringBytes & (ringBytes - 1)) != 0) {
            return false;
        }
        ring = static_cast<BYTE *>(VirtualAlloc(nullptr, ringBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        progress = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (ring == nullptr || wake == nullptr || progress == nullptr) {
            return false;
        }
        size = ringBytes;
        run = runner;
        betweenBatches = between;
        head = tail = sleeping = waiting = quit = 0;
        writeAt = knownTail = 0;
        InitializeCriticalSection(&exec);
        thread = CreateThread(nullptr, 0, work, nullptr, 0, &workerId);
        return thread != nullptr;
    }

    void stop() {
        if (thread == nullptr) {
            return;
        }
        drain();
        InterlockedExchange(&quit, 1);
        SetEvent(wake);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        CloseHandle(wake);
        CloseHandle(progress);
        DeleteCriticalSection(&exec);
        VirtualFree(ring, 0, MEM_RELEASE);
        thread = wake = progress = nullptr;
        ring = nullptr;
        workerId = 0;
    }

    Record *reserve(const uint32_t tag, const uint32_t payload) {
        current = nullptr;
        if (payload > size / 2) {
            return nullptr;
        }
        const uint32_t bytes = (HEADER + payload + 15) & ~15u;
        const uint32_t at = writeAt & (size - 1);
        // the filler is committed on its own: with the record it could need more than the ring holds
        if (at + bytes > size) {
            const uint32_t filler = size - at;
            if (writeAt + filler - knownTail > size && writeAt + filler - (knownTail = load(tail)) > size) {
                waitFor(writeAt + filler - size);
            }
            auto *record = reinterpret_cast<Record *>(ring + at);
            record->bytes = filler;
            record->tag = FILLER;
            writeAt += filler;
            InterlockedExchange(&head, static_cast<LONG>(writeAt));
            wakeWorker();
        }
        if (writeAt + bytes - knownTail > size && writeAt + bytes - (knownTail = load(tail)) > size) {
            waitFor(writeAt + bytes - size);
        }
        current = reinterpret_cast<Record *>(ring + (writeAt & (size - 1)));
        current->bytes = bytes;
        current->tag = tag;
        return current;
    }

    void commit() {
        if (current == nullptr) {
            return;
        }
        writeAt += current->bytes;
        current = nullptr;
        head = static_cast<LONG>(writeAt);
        wakeWorker();
    }

    void drain() {
        if (load(tail) != writeAt) {
            waitFor(writeAt);
        }
    }

    uint32_t position() {
        return writeAt;
    }

    bool waitUntil(const uint32_t target) {
        if (reached(load(tail), target)) {
            return false;
        }
        waitFor(target);
        return true;
    }

    void waitIdle() {
        for (int spin = 0; load(tail) != load(head); spin++) {
            wakeWorker();
            if (spin < SPINS) {
                _mm_pause();
            } else {
                Sleep(0);
            }
        }
    }

    bool idle() {
        return load(tail) == writeAt;
    }

    void lock() {
        if (TryEnterCriticalSection(&exec)) {
            return;
        }
        // as waitFor: the worker may wait inside its batch (Present) for this thread to answer a sent message
        LONG seen = finished;
        ULONGLONG since = GetTickCount64();
        for (int spin = 0; !TryEnterCriticalSection(&exec); spin++) {
            if (spin < SPINS) {
                _mm_pause();
                continue;
            }
            const ULONGLONG now = GetTickCount64();
            if (finished != seen) {
                seen = finished;
                since = now;
            }
            if (now - since < STUCK_MS) {
                Sleep(0);
            } else if (MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_SENDMESSAGE, 0) == WAIT_OBJECT_0) {
                MSG message;
                PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
            }
        }
    }

    void unlock() {
        LeaveCriticalSection(&exec);
    }

    uint32_t workerThread() {
        return workerId;
    }

    uint32_t busyTicks() {
        return static_cast<uint32_t>(busyShown);
    }

    uint32_t waitTicks() {
        return static_cast<uint32_t>(waited >> 10);
    }
}
