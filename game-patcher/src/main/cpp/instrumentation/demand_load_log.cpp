#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "instrumentation/demand_load_log.h"
#include "patch_set.h"
#include "patcher_folder.h"

// Demand load log (debug mode, with -loadlog on the game's command line)
//
// Writes every demand load of the game's asset system to %TEMP%\bfme2-demand-loads.csv: an asset (a texture, a model, ...) asked
// for while it is not loaded yet. The asset's load function (vtable slot 0x2C of the asset base class, 0x00A32AD0) hands such an
// asset to the asset manager (0x00A37320), which waits while the asset's job is queued or worked on by the manager's worker
// thread and otherwise loads it on the calling thread. Each line: milliseconds since the dll was loaded, the thread (main or its
// id), the caller of the load function, the asset's state before and after (bits 16-23 of [asset + 4]: 1 queued, 3 loaded, 4-7
// the load stages, 8 being worked on), the call's duration in milliseconds and the asset's name (slot 0 of its vtable, the call
// the game's own "Demand load" debug message makes at 0x00A37415).

using namespace patching;

namespace {

    constexpr uintptr_t MANAGER_CALL = 0x00A32AED;
    constexpr uintptr_t LOAD_RETURN = 0x00A32AF3;
    constexpr uintptr_t MANAGER_LOAD = 0x00A37320;

    DWORD mainThread = 0;
    LARGE_INTEGER loadedAt = {};
    LARGE_INTEGER frequency = {};
    CRITICAL_SECTION lock;
    FILE *file = nullptr;
    bool failed = false;

    uint32_t stateOf(const uint8_t *asset) {
        return (*reinterpret_cast<const uint32_t *>(asset + 4) >> 16) & 0xFF;
    }

    double millisecondsSince(const LARGE_INTEGER &start, const LARGE_INTEGER &end) {
        return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    }

    uint32_t __stdcall demandLoad(const uintptr_t caller, void *manager, uint8_t *asset) {
        const uint32_t before = stateOf(asset);
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        const uint32_t result = reinterpret_cast<uint32_t (__thiscall *)(void *, void *)>(MANAGER_LOAD)(manager, asset);
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        const uint32_t after = stateOf(asset);

        const auto nameOf = reinterpret_cast<const char *(__thiscall *)(void *)>(**reinterpret_cast<uintptr_t **>(asset));
        const char *name = nameOf(asset);
        const DWORD thread = GetCurrentThreadId();

        EnterCriticalSection(&lock);
        if (file == nullptr && !failed) {
            wchar_t path[MAX_PATH];
            if (!tempFilePath(path, L"bfme2-demand-loads.csv") || _wfopen_s(&file, path, L"w") != 0
                || file == nullptr) {
                failed = true;
            } else {
                fprintf(file, "ms,thread,caller,before,after,duration_ms,name\n");
            }
        }
        if (file != nullptr) {
            char threadName[16];
            if (thread == mainThread) {
                sprintf_s(threadName, "main");
            } else {
                sprintf_s(threadName, "%lu", thread);
            }
            fprintf(file, "%.1f,%s,%08X,%u,%u,%.3f,%s\n", millisecondsSince(loadedAt, start), threadName, static_cast<unsigned>(caller), before,
                    after, millisecondsSince(start, end), name != nullptr ? name : "(null)");
            fflush(file);
        }
        LeaveCriticalSection(&lock);
        return result;
    }
}

bool instrumentation::applyDemandLoadLog() {
    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();

    // at the hook the load function (no frame of its own) has its caller's return address on top of the stack
    const uintptr_t entry = cave.here();
    cave.emit8(0x50); // push eax: the asset
    cave.emit8(0x51); // push ecx: the manager
    const BYTE pushCaller[4] = {0xFF, 0x74, 0x24, 0x08}; // push [esp + 8]: the caller
    cave.emit(pushCaller);
    cave.call(reinterpret_cast<uintptr_t>(&demandLoad));
    cave.jmp(LOAD_RETURN);

    // push eax / call 0x00A37320
    const BYTE managerCall[6] = {0x50, 0xE8, 0x2D, 0x48, 0x00, 0x00};
    set.hook(MANAGER_CALL, managerCall, entry);
    // mov eax, ecx / mov ecx, [eax + 4] / and ecx, 0xFF0000 / cmp ecx, 0x30000: the load function, skipping loaded assets
    const BYTE head[17] = {0x8B, 0xC1, 0x8B, 0x48, 0x04, 0x81, 0xE1, 0x00, 0x00, 0xFF, 0x00, 0x81, 0xF9, 0x00, 0x00, 0x03, 0x00};
    set.expect(0x00A32AD0, head);
    // mov ecx, [0x00DEF75C] / test ecx, ecx / je: the manager
    const BYTE manager[10] = {0x8B, 0x0D, 0x5C, 0xF7, 0xDE, 0x00, 0x85, 0xC9, 0x74, 0x06};
    set.expect(0x00A32AE3, manager);
    const BYTE ret[1] = {0xC3};
    set.expect(LOAD_RETURN, ret);
    // mov eax, [edi] / mov ecx, edi / call [eax]: the manager's own name lookup of an asset
    const BYTE nameCall[6] = {0x8B, 0x07, 0x8B, 0xCF, 0xFF, 0x10};
    set.expect(0x00A37411, nameCall);

    static bool prepared = false;
    if (!prepared) {
        prepared = true;
        mainThread = GetCurrentThreadId();
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&loadedAt);
        InitializeCriticalSection(&lock);
    }
    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
