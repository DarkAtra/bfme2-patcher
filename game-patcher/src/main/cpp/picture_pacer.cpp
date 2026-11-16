#include <windows.h>
#include <intrin.h>
#include <cstdint>

#include "picture_pacer.h"

// Picture pacer (part of the uncapped frame rate, frame_rate_uncap.cpp)
//
// A pass's camera step is made for the time its client update begins, but its picture goes on screen at the first refresh
// after the render thread's worker has presented it, 10 to 17 ms later in a big battle, never the same: on a 180 Hz display a
// picture then stays 2, 3 or 4 refreshes and shows the camera up to a refresh off where it should be (measured, a third of the
// pictures half a refresh or more). So each picture gets its refresh when its pass begins: the first after the usual time to its
// Present (the 75th percentile of the last 32 pictures). The camera steps by the time between the pictures' refreshes, and the
// worker holds each Present (after its batch, outside the exec lock) until the refresh before the picture's: it then goes on screen
// at its own. A picture that is not ready in time goes on screen a refresh late, as without the pacer; one refresh takes one
// picture, the next waits for the next.
//
// An even beat (several refreshes a picture in a big battle, so that each stays as long) was tried and dropped (2026-11-16, note 21):
// with one frame in flight the held Presents held the game thread back (the logic 13 % slower), and the pictures were no more even.
//
// The refreshes come from D3DKMTWaitForVerticalBlankEvent (gdi32) on the primary display, from a thread of its own. Without it, or
// while no worker holds the pictures (the render thread off), schedule says so and the camera steps by the passes' own times.
//
// The idea, pictures on the display's beat and a camera that steps with them, is the BFME2 Accelerator's (Ohad Baehr,
// https://github.com/OhadBaehr/bfme2-accelerator, aotr_pace.inc, aotr_screen.inc, MIT, THIRD-PARTY-NOTICES.txt).

namespace picture_pacer {

    namespace {

        // a picture is held no more than this many refreshes
        constexpr int64_t MOST_AHEAD = 6;
        // the pictures count as held while the worker has held one within this many refreshes
        constexpr int64_t HELD_RECENTLY = 16;

        struct OpenAdapterFromHdc {
            HDC hdc;
            UINT adapter;
            LUID luid;
            UINT source;
        };

        struct WaitForVerticalBlank {
            UINT adapter;
            UINT device;
            UINT source;
        };

        using OpenAdapter = LONG (APIENTRY *)(OpenAdapterFromHdc *);
        using WaitForVblank = LONG (APIENTRY *)(const WaitForVerticalBlank *);

        WaitForVblank waitForVblank = nullptr;
        WaitForVerticalBlank display = {};

        int64_t now() {
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            return counter.QuadPart;
        }
    }

    // The display's refreshes: the last one counted, when it began, and the time between two. Written by the clock's thread, read
    // whole by the others (a sequence lock).
    struct Clock {
        int64_t count;
        int64_t at;
        int64_t period;

        // the refreshes begun by `time`, including those the clock's thread has not counted yet
        int64_t begunBy(const int64_t time) const {
            const int64_t late = time - at - period / 8;
            return late > 0 ? count + late / period : count;
        }

        // the first refresh to begin at or after `time`
        int64_t firstAfter(const int64_t time) const {
            const int64_t ahead = time - at;
            return ahead <= 0 ? count : count + (ahead + period - 1) / period;
        }
    };

    // The usual time from a pass's beginning to its picture's Present: the 75th percentile of the last ones.
    struct Margin {
        static constexpr int SAMPLES = 32;

        int64_t samples[SAMPLES] = {};
        int count = 0;
        int index = 0;

        int64_t add(const int64_t sample) {
            samples[index] = sample;
            index = (index + 1) % SAMPLES;
            if (count < SAMPLES) {
                count++;
            }
            int64_t sorted[SAMPLES];
            for (int i = 0; i < count; i++) {
                int j = i;
                while (j > 0 && sorted[j - 1] > samples[i]) {
                    sorted[j] = sorted[j - 1];
                    j--;
                }
                sorted[j] = samples[i];
            }
            return sorted[count * 3 / 4];
        }
    };

    // the refresh of a picture whose pass begins at `at`: the first after its usual time to the Present, after the picture before's
    int64_t slotFor(const Clock &clock, const int64_t at, const int64_t margin, const int64_t previous) {
        const int64_t slot = clock.firstAfter(at + margin);
        return slot > previous ? slot : previous + 1;
    }

    // whether a picture for `slot` is to be presented now, `begun` refreshes having begun: once the refresh before its own has
    // begun, or when it would have to wait implausibly long
    bool presentNow(const int64_t slot, const int64_t begun) {
        return begun >= slot - 1 || slot - 1 - begun > MOST_AHEAD;
    }

    namespace {

        volatile LONG version = 0;
        Clock clock = {};
        bool running = false;

        // game thread
        bool tried = false;
        Picture current = {};
        int64_t previousSlot = 0;

        // the worker's, the game thread reads them
        volatile LONG margin = 0;
        volatile LONG64 lastHeld = 0;

        // the worker's
        Margin margins;
        Picture pending = {};

        Clock readClock() {
            for (;;) {
                const LONG before = version;
                _ReadWriteBarrier();
                const Clock copy = clock;
                _ReadWriteBarrier();
                if ((before & 1) == 0 && version == before) {
                    return copy;
                }
                YieldProcessor();
            }
        }

        void writeClock(const Clock &value) {
            InterlockedIncrement(&version);
            clock = value;
            InterlockedIncrement(&version);
        }

        DWORD WINAPI countRefreshes(void *) {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
            Clock counted = {};
            for (;;) {
                if (waitForVblank(&display) != 0) {
                    // a mode change or a display that is gone: the clock is stale until refreshes come again
                    counted = {};
                    writeClock(counted);
                    Sleep(100);
                    continue;
                }
                const int64_t at = now();
                if (counted.at != 0) {
                    const int64_t gap = at - counted.at;
                    if (counted.period == 0) {
                        counted.period = gap;
                    } else if (gap > counted.period * 3 / 4 && gap < counted.period * 5 / 4) {
                        counted.period += (gap - counted.period) / 16;
                    }
                    // a refresh this thread slept through still counts
                    counted.count += counted.period > 0 && gap > counted.period * 3 / 2 ? (gap + counted.period / 2) / counted.period : 1;
                } else {
                    counted.count++;
                }
                counted.at = at;
                if (counted.period > 0) {
                    writeClock(counted);
                }
            }
        }

        // false when Windows cannot tell the display's vertical blanks
        bool start() {
            const HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
            const auto open = gdi != nullptr ? reinterpret_cast<OpenAdapter>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc")) : nullptr;
            waitForVblank = gdi != nullptr ? reinterpret_cast<WaitForVblank>(GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent")) : nullptr;
            if (open == nullptr || waitForVblank == nullptr) {
                return false;
            }
            OpenAdapterFromHdc adapter = {};
            adapter.hdc = GetDC(nullptr);
            const LONG opened = open(&adapter);
            ReleaseDC(nullptr, adapter.hdc);
            if (opened != 0) {
                return false;
            }
            display = {adapter.adapter, 0, adapter.source};
            const HANDLE thread = CreateThread(nullptr, 0, countRefreshes, nullptr, 0, nullptr);
            if (thread == nullptr) {
                return false;
            }
            CloseHandle(thread);
            return true;
        }
    }

    bool schedule(const int64_t at, int64_t &step) {
        current = {};
        step = 0;
        if (!tried) {
            tried = true;
            running = start();
        }
        if (!running) {
            return false;
        }
        const Clock refreshes = readClock();
        // stale: the display is gone, or the clock has not begun
        if (refreshes.period == 0 || at - refreshes.at > 8 * refreshes.period) {
            previousSlot = 0;
            return false;
        }
        const int64_t slot = slotFor(refreshes, at, margin, previousSlot);
        const bool stepped = previousSlot != 0;
        step = stepped ? (slot - previousSlot) * refreshes.period : 0;
        const int64_t heldSlot = InterlockedCompareExchange64(&lastHeld, 0, 0);
        const bool held = heldSlot != 0 && slot - heldSlot <= HELD_RECENTLY;
        previousSlot = slot;
        current = {slot, at};
        return held && stepped;
    }

    Picture take() {
        const Picture picture = current;
        current = {};
        return picture;
    }

    void ready(const Picture &picture) {
        const int64_t took = now() - picture.passStart;
        if (took > 0 && took < 0x7FFFFFFF) {
            margin = static_cast<LONG>(margins.add(took));
        }
        pending = picture;
    }

    void hold() {
        if (pending.slot == 0) {
            return;
        }
        const int64_t slot = pending.slot;
        pending = {};
        InterlockedExchange64(&lastHeld, slot);
        int64_t atLeast = 0;
        for (;;) {
            const Clock counted = readClock();
            if (counted.period == 0) {
                return;
            }
            const int64_t begunNow = counted.begunBy(now());
            const int64_t begun = begunNow > atLeast ? begunNow : atLeast;
            if (presentNow(slot, begun) || waitForVblank(&display) != 0) {
                return;
            }
            // a refresh has begun, whether or not the clock's thread has counted it yet
            atLeast = begun + 1;
        }
    }
}
