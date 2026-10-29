#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "render_lock.h"
#include "patch_set.h"

// Render lock
//
// Problem: every call into the renderer's wrapper layer takes and releases a kernel mutex, a system call each time, several
// hundred times per frame.
// Fix: uses a recursive critical section instead, which needs no system call while the lock is free. The mutual exclusion
// with the load screen's thread stays.
//
// Every call of the render's wrapper layer is bracketed by WW3D::Lock (0x0051EEC0) and WW3D::Unlock (0x005208D0), 123 and
// 232 call sites. Each pair waits on and releases a kernel mutex (the handle at 0x00DD1FD8, created in 0x00524FD0), which
// costs a system call each time, and then touches the owner id and recursion count (0x00DD34C8, 0x00DD34CC) under a
// critical section. The mutex exists because the load screen draws from a second thread while a map loads, so mutual
// exclusion has to stay. This patch keeps it and changes the primitive: the four places that wait on or release the
// mutex use a recursive critical section of our own instead, which costs no system call when it is not contended.
//
//   0x0051EEC0  WW3D::Lock          WaitForSingleObject(mutex, 20000)
//   0x0051EF50  try lock            WaitForSingleObject(mutex, timeout), only called by the load screen
//   0x005208D0  WW3D::Unlock        ReleaseMutex(mutex)
//   0x00524FD0  device init         an inline copy of the unlock at its end
//
// The bookkeeping that follows each of them is left as it is, so the owner predicate (0x0051EFA0) answers as before.
// Two differences from the mutex: there is no 20 second timeout message when a thread holds the lock for too long, and
// releasing from a thread that does not hold the lock does nothing, as ReleaseMutex fails without effect, because the
// release checks the owner first.

using namespace patching;

namespace {

    constexpr uintptr_t LOCK = 0x0051EEC0;
    constexpr uintptr_t LOCK_RESUME = 0x0051EED1;
    constexpr uintptr_t TRY_LOCK = 0x0051EF50;
    constexpr uintptr_t TRY_LOCK_RESUME = 0x0051EF62;
    constexpr uintptr_t UNLOCK_RELEASE = 0x00520919;
    constexpr uintptr_t UNLOCK_RESUME = 0x00520926;
    constexpr uintptr_t INIT_RELEASE = 0x0052528B;
    constexpr uintptr_t INIT_RESUME = 0x00525297;

    constexpr uintptr_t ENTER_CRITICAL_SECTION_IAT = 0x00BD0200;
    constexpr uintptr_t LEAVE_CRITICAL_SECTION_IAT = 0x00BD0204;
    constexpr uintptr_t SLEEP_IAT = 0x00BD03B4;

    constexpr uint32_t WAIT_TIMEOUT_RESULT = 0x102;

    struct CaveData {
        CRITICAL_SECTION section;
        uint32_t tryEnterCriticalSection;
        uint32_t getTickCount;
    };

    // The mutex wait of WW3D::Lock: take the section and report WAIT_OBJECT_0.
    uintptr_t emitLock(Cave &cave, const CaveData *data) {
        const uintptr_t entry = cave.here();
        cave.emit8(0x68); // push section
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&data->section)));
        cave.callThrough(ENTER_CRITICAL_SECTION_IAT);
        const BYTE waitObject0[2] = {0x33, 0xC0}; // xor eax, eax
        cave.emit(waitObject0);
        cave.jmp(LOCK_RESUME);
        return entry;
    }

    // The timed wait of the load screen's try lock. `timeout` is the argument of the function, which the cave is
    // entered at the start of. Reports WAIT_OBJECT_0 or WAIT_TIMEOUT to the code that checks it.
    uintptr_t emitTryLock(Cave &cave, const CaveData *data) {
        const uintptr_t section = reinterpret_cast<uintptr_t>(&data->section);
        const uintptr_t tryEnter = reinterpret_cast<uintptr_t>(&data->tryEnterCriticalSection);
        const uintptr_t getTickCount = reinterpret_cast<uintptr_t>(&data->getTickCount);

        const uintptr_t entry = cave.here();

        const BYTE prologue[6] = {
            0x53,                   // push ebx
            0x56,                   // push esi
            0x8B, 0x74, 0x24, 0x0C  // mov esi, [esp + 0xC]   the timeout
        };
        cave.emit(prologue);

        const auto pushSection = [&]() {
            cave.emit8(0x68);
            cave.emit32(static_cast<uint32_t>(section));
        };
        const BYTE testResult[2] = {0x85, 0xC0}; // test eax, eax

        pushSection();
        cave.callThrough(tryEnter);
        cave.emit(testResult);
        const size_t jumpToAcquired = cave.size;
        cave.jcc(JNE, 0); // jnz acquired

        cave.callThrough(getTickCount);
        const BYTE keepStart[2] = {0x8B, 0xD8}; // mov ebx, eax
        cave.emit(keepStart);

        const uintptr_t spin = cave.here();
        cave.callThrough(getTickCount);
        const BYTE elapsed[4] = {
            0x2B, 0xC3, // sub eax, ebx
            0x3B, 0xC6  // cmp eax, esi
        };
        cave.emit(elapsed);
        const size_t jumpToTimeout = cave.size;
        cave.jcc(JAE, 0); // jae timeout

        const BYTE sleepOne[2] = {0x6A, 0x01}; // push 1
        cave.emit(sleepOne);
        cave.callThrough(SLEEP_IAT);
        pushSection();
        cave.callThrough(tryEnter);
        cave.emit(testResult);
        cave.jcc(JE, spin); // jz spin

        const uintptr_t acquired = cave.here();
        cave.resolveJcc(jumpToAcquired, acquired);
        const BYTE waitObject0[2] = {0x33, 0xC0}; // xor eax, eax
        cave.emit(waitObject0);
        const size_t jumpToDone = cave.size;
        cave.jmp(0); // jmp done

        const uintptr_t timedOut = cave.here();
        cave.resolveJcc(jumpToTimeout, timedOut);
        cave.emit8(0xB8); // mov eax, WAIT_TIMEOUT
        cave.emit32(WAIT_TIMEOUT_RESULT);

        const uintptr_t done = cave.here();
        cave.resolveJmp(jumpToDone, done);
        const BYTE epilogue[2] = {0x5E, 0x5B}; // pop esi / pop ebx
        cave.emit(epilogue);
        cave.jmp(TRY_LOCK_RESUME);
        return entry;
    }

    // ReleaseMutex: leave the section if this thread owns it.
    uintptr_t emitRelease(Cave &cave, const CaveData *data, const uintptr_t resume) {
        const uintptr_t section = reinterpret_cast<uintptr_t>(&data->section);
        const uintptr_t owner = section + offsetof(CRITICAL_SECTION, OwningThread);

        const uintptr_t entry = cave.here();
        const BYTE currentThread[6] = {0x64, 0xA1, 0x24, 0x00, 0x00, 0x00}; // mov eax, fs:[0x24]   the thread id
        cave.emit(currentThread);
        const BYTE compare[2] = {0x3B, 0x05}; // cmp eax, [owner]
        cave.emit(compare);
        cave.emit32(static_cast<uint32_t>(owner));
        const BYTE skipLeave[2] = {0x75, 0x0B}; // jne resume, over the push and the call below
        cave.emit(skipLeave);
        cave.emit8(0x68); // push section
        cave.emit32(static_cast<uint32_t>(section));
        cave.callThrough(LEAVE_CRITICAL_SECTION_IAT);
        cave.jmp(resume);
        return entry;
    }
}

bool applyRenderLock() {

    const HMODULE kernel32 = GetModuleHandleA("kernel32");
    const auto tryEnterCriticalSection = reinterpret_cast<uintptr_t>(GetProcAddress(kernel32, "TryEnterCriticalSection"));
    const auto getTickCount = reinterpret_cast<uintptr_t>(GetProcAddress(kernel32, "GetTickCount"));
    if (tryEnterCriticalSection == 0 || getTickCount == 0) {
        return false;
    }

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    auto *data = reinterpret_cast<CaveData *>(caveMemory);
    memset(data, 0, sizeof(CaveData));
    InitializeCriticalSection(&data->section);
    data->tryEnterCriticalSection = static_cast<uint32_t>(tryEnterCriticalSection);
    data->getTickCount = static_cast<uint32_t>(getTickCount);

    Cave cave(caveMemory);
    cave.size = sizeof(CaveData);

    const uintptr_t lockEntry = emitLock(cave, data);
    const uintptr_t tryLockEntry = emitTryLock(cave, data);
    const uintptr_t unlockEntry = emitRelease(cave, data, UNLOCK_RESUME);
    const uintptr_t initEntry = emitRelease(cave, data, INIT_RESUME);

    static PatchSet set;
    set.reset();

    // mov eax, [mutex] / push 20000 / push eax / call [WaitForSingleObject]
    const BYTE stockLock[17] = {
        0xA1, 0xD8, 0x1F, 0xDD, 0x00,
        0x68, 0x20, 0x4E, 0x00, 0x00,
        0x50,
        0xFF, 0x15, 0x34, 0x02, 0xBD, 0x00
    };
    set.hook(LOCK, stockLock, lockEntry);

    // mov eax, [esp + 4] / mov ecx, [mutex] / push eax / push ecx / call [WaitForSingleObject]
    const BYTE stockTryLock[18] = {
        0x8B, 0x44, 0x24, 0x04,
        0x8B, 0x0D, 0xD8, 0x1F, 0xDD, 0x00,
        0x50,
        0x51,
        0xFF, 0x15, 0x34, 0x02, 0xBD, 0x00
    };
    set.hook(TRY_LOCK, stockTryLock, tryLockEntry);

    // mov edx, [mutex] / push edx / call [ReleaseMutex]
    const BYTE stockUnlock[13] = {
        0x8B, 0x15, 0xD8, 0x1F, 0xDD, 0x00,
        0x52,
        0xFF, 0x15, 0x24, 0x02, 0xBD, 0x00
    };
    set.hook(UNLOCK_RELEASE, stockUnlock, unlockEntry);

    // mov eax, [mutex] / push eax / call [ReleaseMutex]
    const BYTE stockInitRelease[12] = {
        0xA1, 0xD8, 0x1F, 0xDD, 0x00,
        0x50,
        0xFF, 0x15, 0x24, 0x02, 0xBD, 0x00
    };
    set.hook(INIT_RELEASE, stockInitRelease, initEntry);

    // cmp eax, WAIT_TIMEOUT, the first instruction after the waits
    const BYTE afterWait[5] = {0x3D, 0x02, 0x01, 0x00, 0x00};
    set.expect(LOCK_RESUME, afterWait);
    set.expect(TRY_LOCK_RESUME, afterWait);

    // mov al, bl / pop ebx / ret
    const BYTE afterUnlock[4] = {0x8A, 0xC3, 0x5B, 0xC3};
    set.expect(UNLOCK_RESUME, afterUnlock);

    // mov ecx, [esp + 0x14]
    const BYTE afterInit[4] = {0x8B, 0x4C, 0x24, 0x14};
    set.expect(INIT_RESUME, afterInit);

    if (!commit(set, cave)) {
        DeleteCriticalSection(&data->section);
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
