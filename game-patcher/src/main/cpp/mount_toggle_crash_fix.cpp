#include <windows.h>
#include <cstdint>

#include "mount_toggle_crash_fix.h"
#include "patch_set.h"

// Mount toggle crash fix
//
// Problem: the game crashes when a unit mounts, dismounts or changes form (e.g. a hero becoming a Ring hero) while its last
// order was to move to an object that no longer exists.
// Fix: an order whose target is gone is not handed to the new form of the unit.
//
// When ToggleMountedSpecialAbilityUpdate swaps a unit for its other template, it asks 0x0066C2BF (callers 0x008B1644 and
// 0x008B24C3) whether the unit's last order is worth giving to the new object. The order is rebuilt from the ids it stored,
// and an id whose object was destroyed comes back as a null pointer (0x0075315B never checks). For a move to an object the
// check then reads the target's position through it (0x0066C410). The skirmish AI walks a hero with PickupStuffUpdate to the
// One Ring, the Ring is destroyed on pickup, and the hero's stored order still names it when the Ring hero toggle fires.
// With no target the check now answers "not worth transferring" (bl = 1, read back at the function's end), the same answer
// as an order that is not moving anywhere. Derived from pySAGE's ai-command-null-target (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t TRANSFER_CHECK = 0x0066C2BF;
    constexpr uintptr_t ANSWER_SEED = 0x0066C2D1;
    constexpr uintptr_t TARGET_LOAD = 0x0066C40D;
    constexpr uintptr_t TARGET_POSITION = 0x0066C410;
    constexpr uintptr_t TARGET_POSITION_RESUME = 0x0066C415;
    constexpr uintptr_t CLEANUP = 0x0066C470;
    constexpr uintptr_t ANSWER_READ = 0x0066C483;
    constexpr uintptr_t MOUNT_CALL = 0x008B1644;
    constexpr uintptr_t DISMOUNT_CALL = 0x008B24C3;
}

bool applyMountToggleCrashFix() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // eax is the target object
    Cave cave(caveMemory);
    const BYTE targetTest[9] = {
        0x85, 0xC0, // test eax, eax
        0x74, 0x0A, // je noTarget
        0xF3, 0x0F, 0x5C, 0x40, 0x38 // subss xmm0, [eax + 0x38] (the instruction the hook replaced)
    };
    cave.emit(targetTest);
    cave.jmp(TARGET_POSITION_RESUME);
    const BYTE notWorthTransferring[2] = {0xB3, 0x01}; // noTarget: mov bl, 1
    cave.emit(notWorthTransferring);
    cave.jmp(CLEANUP);

    static PatchSet set;
    set.reset();

    // mov eax, 0x00B872FC / call: the exception frame's setup
    const BYTE head[10] = {0xB8, 0xFC, 0x72, 0xB8, 0x00, 0xE8, 0x27, 0x0C, 0x3D, 0x00};
    set.expect(TRANSFER_CHECK, head);

    // xor ebx, ebx / inc ebx: the answer starts as "not worth transferring"
    const BYTE answerSeed[3] = {0x33, 0xDB, 0x43};
    set.expect(ANSWER_SEED, answerSeed);

    // mov eax, [ebp + 0x1C]: the order's target object
    const BYTE targetLoad[3] = {0x8B, 0x45, 0x1C};
    set.expect(TARGET_LOAD, targetLoad);

    // subss xmm0, [eax + 0x38]
    const BYTE targetPosition[5] = {0xF3, 0x0F, 0x5C, 0x40, 0x38};
    set.hook(TARGET_POSITION, targetPosition, reinterpret_cast<uintptr_t>(caveMemory));

    // subss xmm1, [eax + 0x3C]
    const BYTE resume[5] = {0xF3, 0x0F, 0x5C, 0x48, 0x3C};
    set.expect(TARGET_POSITION_RESUME, resume);

    // or dword [ebp - 4], -1: the exception frame's state, then the order's waypoints are freed
    const BYTE cleanup[4] = {0x83, 0x4D, 0xFC, 0xFF};
    set.expect(CLEANUP, cleanup);

    // mov al, bl
    const BYTE answerRead[2] = {0x8A, 0xC3};
    set.expect(ANSWER_READ, answerRead);

    // call TRANSFER_CHECK, from the mount and the dismount swap
    const BYTE mountCall[5] = {0xE8, 0x76, 0xAC, 0xDB, 0xFF};
    set.expect(MOUNT_CALL, mountCall);
    const BYTE dismountCall[5] = {0xE8, 0xF7, 0x9D, 0xDB, 0xFF};
    set.expect(DISMOUNT_CALL, dismountCall);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
