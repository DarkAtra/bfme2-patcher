#include <windows.h>
#include <cstdint>

#include "mount_health_ratio.h"
#include "patch_set.h"

// Mount health ratio
//
// Problem: mounting and dismounting (ToggleMountedSpecialAbilityUpdate with MountedTemplate) hand the unit's health to its
// other form as hit points, so toggling between two templates with different MaxHealth heals or hurts the unit.
// Fix: the health is handed over as a fraction of the maximum health.
//
// Both swaps (mount 0x008B14F0, dismount 0x008B236F) run the same sequence: keep the new object's body at [ebp - 0x14], ask the
// old body for its health (body vtable + 0x10), and set that on the new body (+ 0xAC). The hook replaces the health call with
// old health ratio (+ 0x14) times new maximum (+ 0x1C), returned in st(0) like the call it replaces. A unit above its maximum
// crosses over at the same fraction above the new one. Derived from pySAGE's mount-health-ratio
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t MOUNT_HEALTH_COPY = 0x008B14F0;
    constexpr uintptr_t DISMOUNT_HEALTH_COPY = 0x008B236F;
    constexpr size_t HEALTH_CALL = 0x11;

    void addHealthCopy(PatchSet &set, const uintptr_t sequence, const uintptr_t cave) {
        // mov eax, [esi + 0x25C] / mov ecx, [edi + 0x25C] / mov ebx, [eax] / mov [ebp - 0x14], eax: the new and the old body
        const BYTE bodies[17] = {0x8B, 0x86, 0x5C, 0x02, 0x00, 0x00, 0x8B, 0x8F, 0x5C, 0x02, 0x00, 0x00, 0x8B, 0x18, 0x89, 0x45, 0xEC};
        set.expect(sequence, bodies);
        // mov eax, [ecx] / call [eax + 0x10]: the old body's health
        const BYTE health[5] = {0x8B, 0x01, 0xFF, 0x50, 0x10};
        BYTE call[5] = {0xE8};
        const auto displacement = static_cast<int32_t>(cave - (sequence + HEALTH_CALL + 5));
        memcpy(call + 1, &displacement, sizeof(displacement));
        set.replace(sequence + HEALTH_CALL, health, call);
        // push ecx / mov ecx, [ebp - 0x14] / fstp dword [esp] / call [ebx + 0xAC]: set it on the new body
        const BYTE setHealth[13] = {0x51, 0x8B, 0x4D, 0xEC, 0xD9, 0x1C, 0x24, 0xFF, 0x93, 0xAC, 0x00, 0x00, 0x00};
        set.expect(sequence + HEALTH_CALL + 5, setHealth);
    }
}

bool applyMountHealthRatio() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // ecx is the old body, [ebp - 0x14] the new one. The ratio waits on the stack while the second call runs, since the x87
    // stack has to be empty at a call.
    Cave cave(caveMemory);
    const BYTE ratioTimesMaximum[22] = {
        0x8B, 0x01, // mov eax, [ecx]
        0xFF, 0x50, 0x14, // call [eax + 0x14]: the old body's health ratio
        0x51, // push ecx
        0xD9, 0x1C, 0x24, // fstp dword [esp]
        0x8B, 0x4D, 0xEC, // mov ecx, [ebp - 0x14]
        0x8B, 0x01, // mov eax, [ecx]
        0xFF, 0x50, 0x1C, // call [eax + 0x1C]: the new body's maximum health
        0xD8, 0x0C, 0x24, // fmul dword [esp]
        0x59, // pop ecx
        0xC3 // ret
    };
    cave.emit(ratioTimesMaximum);

    static PatchSet set;
    set.reset();
    addHealthCopy(set, MOUNT_HEALTH_COPY, reinterpret_cast<uintptr_t>(caveMemory));
    addHealthCopy(set, DISMOUNT_HEALTH_COPY, reinterpret_cast<uintptr_t>(caveMemory));

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
