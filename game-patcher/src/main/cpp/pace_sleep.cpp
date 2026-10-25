#include <windows.h>
#include <cstdint>
#include <cstring>

#include "features.h"
#include "pace_sleep.h"
#include "patch_set.h"
#include "stats.h"

// The frame limiter waits for the end of a frame by calling Sleep(0) in a loop, which returns at once and keeps the main
// thread at 100% for most of every frame. This patch sleeps through the time that is left instead, and keeps spinning on
// Sleep(0) only for the last SPIN_MARGIN_MS milliseconds so a late wake-up does not stretch the frame. The frame rate is
// the same, the core is not pinned. The game raises the timer resolution to 1 ms at start (timeBeginPeriod in the
// GameEngine constructor), so Sleep(n) returns after n to n + 1 ms.
//
// The limiter loop is at 0x0063A1DC. At that point esi is the period the frame has to take, edi the time of the last
// timeGetTime call and [0x00DE4318] the time the frame started.

using namespace patching;

namespace {

    constexpr uintptr_t LOOP_HEAD = 0x0063A1DC;
    constexpr uintptr_t LOOP_RESUME = 0x0063A1E3;
    constexpr uintptr_t LOOP_BRANCH = 0x0063A1F1;
    constexpr uintptr_t FRAME_START_TIME = 0x00DE4318;
    constexpr uintptr_t SLEEP_IAT = 0x00BD03B4;

    constexpr BYTE SPIN_MARGIN_MS = 2;
}

bool applyPaceSleep() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    Stats *counters = features::STATS ? stats() : nullptr;

    const uintptr_t entry = cave.here();

    // eax = period - (now - frame start) - SPIN_MARGIN_MS, or 0 if that is not positive
    const BYTE elapsed[4] = {
        0x8B, 0xC7, // mov eax, edi
        0x2B, 0x05  // sub eax, [frame start]
    };
    cave.emit(elapsed);
    cave.emit32(FRAME_START_TIME);
    const BYTE remaining[11] = {
        0xF7, 0xD8,                // neg eax
        0x03, 0xC6,                // add eax, esi
        0x83, 0xE8, SPIN_MARGIN_MS, // sub eax, SPIN_MARGIN_MS
        0x7F, 0x02,                // jg sleep
        0x33, 0xC0                 // xor eax, eax
    };
    cave.emit(remaining);
    // sleep:
    if (counters != nullptr) {
        cave.increment(reinterpret_cast<uintptr_t>(&counters->paceLoopIterations));
        cave.emit8(0x01); // add [paceSleepMs], eax
        cave.emit8(0x05);
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&counters->paceSleepMs)));
    }
    cave.emit8(0x50); // push eax
    cave.emit8(0xFF); // call [Sleep]
    cave.emit8(0x15);
    cave.emit32(SLEEP_IAT);
    cave.jmp(LOOP_RESUME);

    static PatchSet set;
    set.reset();

    // push ebx / call [Sleep]
    const BYTE stockSleep[7] = {0x53, 0xFF, 0x15, 0xB4, 0x03, 0xBD, 0x00};
    set.hook(LOOP_HEAD, stockSleep, entry);

    // call [timeGetTime]
    const BYTE timeGetTimeCall[6] = {0xFF, 0x15, 0x20, 0x09, 0xBD, 0x00};
    set.expect(LOOP_RESUME, timeGetTimeCall);

    // cmp eax, esi / jb LOOP_HEAD
    const BYTE loopBranch[4] = {0x3B, 0xC6, 0x72, 0xE7};
    set.expect(LOOP_BRANCH, loopBranch);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
