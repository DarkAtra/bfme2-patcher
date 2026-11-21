#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>

#include "patch_set.h"
#include "render_recorder.h"
#include "render_thread.h"

// Render thread (tools/analysis/notes/20-accelerator-port.md)
//
// Problem: the game thread spends about a third of its work in a big battle inside Direct3D 9, D3DX and the driver, one call after
// another, while the other cores idle.
// Fix: the game thread records its Direct3D calls into a queue and a second thread runs them in the same order, so the picture is
// the same and the game thread goes on with the next frame (render_recorder.cpp, render_queue.cpp, d3d_methods.cpp). A port of the
// render thread of Ohad Baehr's BFME2 Accelerator (https://github.com/OhadBaehr/bfme2-accelerator, MIT, THIRD-PARTY-NOTICES.txt),
// studied in pySAGE's fork (https://github.com/ClementJ18/bfme2-accelerator, branch edain-compat).
//
// This file: the device is created thread-safe (D3DCREATE_MULTITHREADED), because calls that stay on the game thread (reference
// counts, descriptions) run while the render thread is inside Direct3D. The game reads its device's behaviour flags from 0x00DD345C
// for both of its CreateDevice calls (0x005241B6, 0x00524222); the loads at 0x00524190 and 0x005241EF call a stub that adds the
// flag. Both successful paths then call the device setup 0x00523F50 from 0x0052422D, with the new device in 0x00DD3474; that call
// goes through a stub that hands the device's vtable to the recording first (render_recorder.cpp), which starts with the patch.
// The patch refuses under the Windows XP compatibility layer: apphelp.dll then owns the device's vtable and swaps D3D9's own
// functions back in around its calls, which no vtable hook survives (the setting is only read, never changed).

using namespace patching;

namespace {

    // the two loads of the behaviour flags, one before each CreateDevice call
    constexpr uintptr_t FLAGS_LOADS[2] = {0x00524190, 0x005241EF};
    constexpr uintptr_t BEHAVIOUR_FLAGS = 0x00DD345C;
    constexpr uint32_t D3DCREATE_MULTITHREADED = 0x00000004;
    // call 0x00523F50, the device setup after a successful CreateDevice
    constexpr uintptr_t DEVICE_SETUP_CALL = 0x0052422D;
    constexpr uintptr_t DEVICE_SETUP = 0x00523F50;
    constexpr uintptr_t DEVICE = 0x00DD3474;

    uint32_t gameThread = 0;

    void __stdcall deviceCreated(const uint32_t device) {
        if (device != 0) {
            render_recorder::hookDevice(device);
        }
    }

    bool containsXpLayer(const wchar_t *layers) {
        for (const wchar_t *at = layers; *at != L'\0'; at++) {
            if (_wcsnicmp(at, L"WINXP", 5) == 0) {
                return true;
            }
        }
        return false;
    }

    using RegGetValueFunction = LSTATUS (WINAPI *)(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD);

    bool registryXpLayer(const RegGetValueFunction getValue, const HKEY root, const wchar_t *exe) {
        wchar_t layers[512];
        DWORD bytes = sizeof(layers) - sizeof(wchar_t);
        if (getValue(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", exe, RRF_RT_REG_SZ, nullptr, layers,
                     &bytes) != ERROR_SUCCESS) {
            return false;
        }
        layers[bytes / sizeof(wchar_t)] = L'\0';
        return containsXpLayer(layers);
    }

    // the layers of this process: the environment carries them from a launcher, the registry holds the exe's own
    bool runsUnderXpLayer() {
        wchar_t layers[512];
        const DWORD length = GetEnvironmentVariableW(L"__COMPAT_LAYER", layers, sizeof(layers) / sizeof(layers[0]));
        if (length > 0 && length < sizeof(layers) / sizeof(layers[0]) && containsXpLayer(layers)) {
            return true;
        }
        wchar_t exe[MAX_PATH];
        const DWORD exeLength = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (exeLength == 0 || exeLength >= MAX_PATH) {
            return false;
        }
        // from kernelbase.dll, which every process has (since Windows 7), so that the dll needs no advapi32 import
        const HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        const auto getValue = kernelBase != nullptr ? reinterpret_cast<RegGetValueFunction>(GetProcAddress(kernelBase, "RegGetValueW")) : nullptr;
        if (getValue == nullptr) {
            return false;
        }
        return registryXpLayer(getValue, HKEY_CURRENT_USER, exe) || registryXpLayer(getValue, HKEY_LOCAL_MACHINE, exe);
    }
}

uint32_t renderThreadGameThread() {
    return gameThread;
}

bool applyRenderThread() {
    if (runsUnderXpLayer()) {
        return false;
    }

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);
    // mov ecx, [BEHAVIOUR_FLAGS] / or ecx, D3DCREATE_MULTITHREADED / ret
    const BYTE stub[2] = {0x8B, 0x0D};
    cave.emit(stub);
    cave.emit32(static_cast<uint32_t>(BEHAVIOUR_FLAGS));
    const BYTE addFlag[4] = {0x83, 0xC9, static_cast<BYTE>(D3DCREATE_MULTITHREADED), 0xC3};
    cave.emit(addFlag);
    // push [DEVICE] / call deviceCreated / jmp DEVICE_SETUP (the setup reads no register it is called with)
    const uintptr_t setupStub = cave.here();
    const BYTE pushDevice[2] = {0xFF, 0x35};
    cave.emit(pushDevice);
    cave.emit32(static_cast<uint32_t>(DEVICE));
    cave.call(reinterpret_cast<uintptr_t>(&deviceCreated));
    cave.jmp(DEVICE_SETUP);

    static PatchSet set;
    set.reset();
    // mov ecx, [BEHAVIOUR_FLAGS], replaced by call stub / nop (EFLAGS, which the stub's or changes, is not read before the
    // CreateDevice call's result is tested)
    const BYTE load[6] = {0x8B, 0x0D, 0x5C, 0x34, 0xDD, 0x00};
    for (const uintptr_t site : FLAGS_LOADS) {
        BYTE call[6] = {0xE8, 0x00, 0x00, 0x00, 0x00, 0x90};
        const auto displacement = static_cast<int32_t>(reinterpret_cast<uintptr_t>(caveMemory) - (site + 5));
        memcpy(call + 1, &displacement, sizeof(displacement));
        set.replace(site, load, call);
    }
    const BYTE setupCall[5] = {0xE8, 0x1E, 0xFD, 0xFF, 0xFF};
    set.redirectCall(DEVICE_SETUP_CALL, setupCall, setupStub);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    // the worker starts after the commit: under the loader lock a started worker could not be stopped again; the game's code
    // has not run yet, so nothing can be inside the stubs a revert takes out
    if (!render_recorder::start()) {
        revert(set, cave);
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    // the dll is loaded while the process starts, on the thread that runs the game
    gameThread = GetCurrentThreadId();
    return true;
}
