#include <windows.h>
#include <cstdint>

#include "hero_recruit_queue.h"
#include "patch_set.h"

// Hero recruits and the queue
//
// Problem: a hero being recruited at the front of a building's queue holds up every unit and upgrade queued behind it until
// the hero is ready.
// Fix: the building works on the first entry that is not a hero recruit. The hero's recruit time is unchanged, and a ready
// hero still leaves first.
//
// ProductionUpdate works on one queue entry per frame, chosen by 0x008A072F (called from 0x008A1C17). Its last rules hand out a
// hero recruit (entry kind 3) whose clock in the player's revive manager has run out, and otherwise the queue's head
// (0x008A07D5). A hero recruit's clock runs on the game frame from the moment it was queued (0x007812B2), whether it is worked on
// or not, so working on the head only blocks the entries behind it. The fallback now returns the first entry that is not a hero
// recruit, and the head only when all of them are. Derived from pySAGE's hero-recruit-parallel
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t PICK = 0x008A072F;
    constexpr uintptr_t PICK_CALL = 0x008A1C17;
    constexpr uintptr_t REVIVE_SCAN = 0x008A078A;
    constexpr uintptr_t REVIVE_READY = 0x008A07B7;
    constexpr uintptr_t FALLBACK = 0x008A07D1;
    constexpr uintptr_t PICK_RETURN = 0x008A07D8;
    // thiscall(queue interface, entry), ret 4: the entry's next
    constexpr uintptr_t NEXT_ENTRY = 0x008A1904;
    constexpr uintptr_t NEXT_ENTRY_SLOT = 0x00C67E08;
    constexpr uintptr_t REVIVE_PROGRESS = 0x00780C9F;
    constexpr uintptr_t REVIVE_START = 0x007812B2;

    constexpr BYTE REVIVE_ENTRY = 3;
}

bool applyHeroRecruitQueue() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // esi is the production module, ebx the entry the ready hero scan looks at next; ebx, edi and esi are popped at PICK_RETURN
    Cave cave(caveMemory);
    const BYTE scanNext[4] = {
        0x85, 0xDB, // test ebx, ebx
        0x74, 0x05 // je fallback
    };
    cave.emit(scanNext);
    cave.jmp(REVIVE_SCAN);
    const BYTE fallback[23] = {
        0x8B, 0x46, 0x28, // fallback: mov eax, [esi + 0x28]: the head
        0x85, 0xC0, // test eax, eax
        0x74, 0x10, // je done
        0x83, 0x78, 0x04, REVIVE_ENTRY, // scan: cmp dword [eax + 4], 3
        0x75, 0x0A, // jne done
        0x8B, 0x40, 0x48, // mov eax, [eax + 0x48]: the next entry
        0x85, 0xC0, // test eax, eax
        0x75, 0xF3, // jne scan
        0x8B, 0x46, 0x28 // mov eax, [esi + 0x28]: only hero recruits, the head as before
    };
    cave.emit(fallback);
    cave.jmp(PICK_RETURN); // done

    static PatchSet set;
    set.reset();

    // push esi / mov esi, ecx / mov eax, [esi + 0x28]
    const BYTE pick[6] = {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x28};
    set.expect(PICK, pick);
    const BYTE pickCall[5] = {0xE8, 0x13, 0xEB, 0xFF, 0xFF};
    set.expect(PICK_CALL, pickCall);

    // cmp dword [ebx + 4], 3: the ready hero scan
    const BYTE reviveScan[4] = {0x83, 0x7B, 0x04, REVIVE_ENTRY};
    set.expect(REVIVE_SCAN, reviveScan);
    // call REVIVE_PROGRESS: the hero's clock in the revive manager
    const BYTE reviveReady[5] = {0xE8, 0xE3, 0x04, 0xEE, 0xFF};
    set.expect(REVIVE_READY, reviveReady);

    // test ebx, ebx / jne REVIVE_SCAN / mov eax, [esi + 0x28]
    const BYTE fallbackStock[7] = {0x85, 0xDB, 0x75, 0xB5, 0x8B, 0x46, 0x28};
    set.hook(FALLBACK, fallbackStock, reinterpret_cast<uintptr_t>(caveMemory));

    // pop ebx / pop edi / pop esi / ret
    const BYTE pickReturn[4] = {0x5B, 0x5F, 0x5E, 0xC3};
    set.expect(PICK_RETURN, pickReturn);

    // mov eax, [esp + 4] / test eax, eax / je / mov eax, [eax + 0x48]
    const BYTE nextEntry[11] = {0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x74, 0x05, 0x8B, 0x40, 0x48};
    set.expect(NEXT_ENTRY, nextEntry);
    BYTE nextEntryAddress[4];
    putInt(nextEntryAddress, static_cast<int>(NEXT_ENTRY));
    set.expect(NEXT_ENTRY_SLOT, nextEntryAddress);

    const BYTE reviveProgress[4] = {0x55, 0x8B, 0xEC, 0x56};
    set.expect(REVIVE_PROGRESS, reviveProgress);
    const BYTE reviveStart[5] = {0x56, 0x8B, 0x74, 0x24, 0x0C};
    set.expect(REVIVE_START, reviveStart);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
