#include <windows.h>
#include <cstdint>

#include "hero_battalion_absorption.h"
#include "patch_set.h"

// Heroes joining battalions
//
// Problem: a hero who finishes while a battalion is still walking out of the same building becomes part of that battalion and
// loses its rally point.
// Fix: an object only joins the battalion in the door when the battalion has a free slot its template fits, which is the
// game's own membership rule.
//
// QueueProductionExitUpdate remembers the battalion that is walking out ([exit interface + 0x20], 0x008A4267) for the whole
// battalion entry, and exitObjectViaDoor (0x008A3DD5) binds every object that leaves meanwhile to it (0x008A4036 onwards:
// producer, team, formation slot, "not a lone unit"), with no test. With hero_recruit_queue a hero can finish while a battalion
// is leaving. The hook replaces the lookup of the remembered battalion with one that answers "none" unless the battalion has a
// free slot whose payload template the leaving object is equivalent to, walked the way the battalion's own slot assignment
// does it (0x00873F30). Not fixed: the hero's entry may still end the battalion's exit early, so the rest of the battalion
// leaves loose. Derived from pySAGE's horde-exit-absorption (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t EXIT_VIA_DOOR = 0x008A3DD5;
    constexpr uintptr_t BATTALION_LOOKUP = 0x008A4036;
    constexpr uintptr_t BIND_BLOCK = 0x008A4066;
    constexpr uintptr_t LONE_UNIT_FLAG = 0x008A4169;
    constexpr uintptr_t REMEMBER_BATTALION = 0x008A4267;
    // thiscall(game logic, id), ret 4
    constexpr uintptr_t FIND_OBJECT_BY_ID = 0x00449681;
    // the battalion contain's interface (vtable + 0x7C of a contain module)
    constexpr uintptr_t BATTALION_INTERFACE = 0x00872A6F;
    // the battalion's own slot assignment, which the walk repeats
    constexpr uintptr_t ASSIGN_SLOT = 0x00873F30;
    // thiscall(module data, payload key), ret 4
    constexpr uintptr_t PAYLOAD_LOOKUP = 0x0086C3E7;
    // thiscall(thing factory, name), ret 4
    constexpr uintptr_t FIND_TEMPLATE = 0x006D1305;
    // thiscall(template, other), ret 4
    constexpr uintptr_t IS_EQUIVALENT = 0x0073D5C2;

    size_t forwardJcc(Cave &cave, const BYTE condition) {
        const size_t offset = cave.size;
        cave.jcc(condition, 0);
        return offset;
    }
}

bool applyHeroBattalionAbsorption() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // Stands in for findObjectByID: [esp + 4] the remembered battalion's id, ecx TheGameLogic, ret 4. ebx is the leaving object
    // and edi the exit interface; both come back untouched. The walk uses the slot assignment's registers: esi the battalion
    // interface, edi the free slot list node.
    Cave cave(caveMemory);
    const auto base = reinterpret_cast<uintptr_t>(caveMemory);
    const BYTE lookup[4] = {0xFF, 0x74, 0x24, 0x04}; // push dword [esp + 4]
    cave.emit(lookup);
    cave.call(FIND_OBJECT_BY_ID);
    const BYTE battalionTest[2] = {0x85, 0xC0}; // test eax, eax
    cave.emit(battalionTest);
    const size_t noBattalion = forwardJcc(cave, JE);
    const BYTE contain[11] = {
        0x56, 0x57, 0x50, // push esi / push edi / push eax: the battalion, popped as the answer
        0x8B, 0x88, 0x58, 0x02, 0x00, 0x00, // mov ecx, [eax + 0x258]: its contain
        0x85, 0xC9 // test ecx, ecx
    };
    cave.emit(contain);
    const size_t noContain = forwardJcc(cave, JE);
    const BYTE battalionInterface[7] = {
        0x8B, 0x01, // mov eax, [ecx]
        0xFF, 0x50, 0x7C, // call [eax + 0x7C]: the battalion interface
        0x85, 0xC0 // test eax, eax
    };
    cave.emit(battalionInterface);
    const size_t noInterface = forwardJcc(cave, JE);
    const BYTE slotsBuilt[6] = {
        0x8B, 0xF0, // mov esi, eax
        0x80, 0x7E, 0x7C, 0x00 // cmp byte [esi + 0x7C], 0: the slots are built
    };
    cave.emit(slotsBuilt);
    const size_t built = forwardJcc(cave, JNE);
    const BYTE buildSlots[16] = {
        0x8D, 0x8E, 0xE4, 0xFE, 0xFF, 0xFF, // lea ecx, [esi - 0x11C]: the contain module
        0x8B, 0x01, // mov eax, [ecx]
        0x6A, 0x01, // push 1
        0xFF, 0x90, 0x84, 0x00, 0x00, 0x00 // call [eax + 0x84]: build the slots
    };
    cave.emit(buildSlots);

    const uintptr_t walk = cave.here();
    const BYTE freeSlots[7] = {
        0x8B, 0x46, 0x78, // mov eax, [esi + 0x78]: the free slots
        0x8B, 0x38, // mov edi, [eax]
        0x3B, 0xF8 // cmp edi, eax
    };
    cave.emit(freeSlots);
    const size_t noFreeSlot = forwardJcc(cave, JE);

    const uintptr_t slot = cave.here();
    const BYTE payload[17] = {
        0x8B, 0x47, 0x08, // mov eax, [edi + 8]: the slot's index
        0x6B, 0xC0, 0x1C, // imul eax, eax, 0x1C
        0x03, 0x46, 0x6C, // add eax, [esi + 0x6C]: the slot
        0xFF, 0x30, // push dword [eax]: its payload key
        0x8B, 0x8E, 0xE8, 0xFE, 0xFF, 0xFF // mov ecx, [esi - 0x118]: the module data
    };
    cave.emit(payload);
    cave.call(PAYLOAD_LOOKUP);
    cave.emit(battalionTest);
    const size_t noPayload = forwardJcc(cave, JE);
    const BYTE templateName[10] = {
        0x8B, 0x0D, 0x40, 0x4A, 0xDE, 0x00, // mov ecx, [0x00DE4A40]: TheThingFactory
        0x83, 0xC0, 0x04, // add eax, 4: the payload's template name
        0x50 // push eax
    };
    cave.emit(templateName);
    cave.call(FIND_TEMPLATE);
    cave.emit(battalionTest);
    const size_t noTemplate = forwardJcc(cave, JE);
    const BYTE equivalent[4] = {
        0x8B, 0x4B, 0x04, // mov ecx, [ebx + 4]: the leaving object's template
        0x50 // push eax
    };
    cave.emit(equivalent);
    cave.call(IS_EQUIVALENT);
    const BYTE fits[2] = {0x84, 0xC0}; // test al, al
    cave.emit(fits);
    const size_t member = forwardJcc(cave, JNE);

    const uintptr_t nextSlot = cave.here();
    const BYTE next[5] = {
        0x8B, 0x3F, // mov edi, [edi]
        0x3B, 0x7E, 0x78 // cmp edi, [esi + 0x78]
    };
    cave.emit(next);
    cave.jcc(JNE, slot);

    const uintptr_t reject = cave.here();
    const BYTE notMember[6] = {
        0x58, // pop eax
        0x33, 0xC0, // xor eax, eax: no battalion is leaving through this door for this object
        0xEB, 0x01, // jmp restore
        0x58 // keep: pop eax: the battalion
    };
    cave.emit(notMember);
    const uintptr_t keep = reject + 5;
    const BYTE restore[5] = {0x5F, 0x5E, 0xC2, 0x04, 0x00}; // restore: pop edi / pop esi / out: ret 4
    cave.emit(restore);
    const uintptr_t out = cave.here() - 3;

    cave.resolveJcc(noBattalion, out);
    cave.resolveJcc(noContain, keep);
    cave.resolveJcc(noInterface, keep);
    cave.resolveJcc(built, walk);
    cave.resolveJcc(noFreeSlot, reject);
    cave.resolveJcc(noPayload, nextSlot);
    cave.resolveJcc(noTemplate, nextSlot);
    cave.resolveJcc(member, keep);

    static PatchSet set;
    set.reset();

    // mov eax, 0x00BA211E / call: the exception frame's setup
    const BYTE exitViaDoor[5] = {0xB8, 0x1E, 0x21, 0xBA, 0x00};
    set.expect(EXIT_VIA_DOOR, exitViaDoor);
    // push [edi + 0x20] (the remembered battalion) / mov ecx, [TheGameLogic]
    const BYTE lookupArguments[9] = {0xFF, 0x77, 0x20, 0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00};
    set.expect(BATTALION_LOOKUP - 9, lookupArguments);
    const BYTE lookupCall[5] = {0xE8, 0x46, 0x56, 0xBA, 0xFF};
    set.redirectCall(BATTALION_LOOKUP, lookupCall, base);
    // push [ebp - 0x1C] / mov ecx, ebx / call (set producer) / mov eax, [esi] / push ebx / mov ecx, esi / call [eax + 0x2C]
    const BYTE bindBlock[18] = {0xFF, 0x75, 0xE4, 0x8B, 0xCB, 0xE8, 0x31, 0x76, 0xDE, 0xFF, 0x8B, 0x06, 0x53, 0x8B, 0xCE, 0xFF, 0x50, 0x2C};
    set.expect(BIND_BLOCK, bindBlock);
    // mov eax, [ebx + 4] / test byte [eax + 0x115], 0x20 / jne / cmp dword [ebp - 0x1C], 0 / mov byte [ebp + 0xB], 1
    const BYTE loneUnitFlag[20] = {0x8B, 0x43, 0x04, 0xF6, 0x80, 0x15, 0x01, 0x00, 0x00, 0x20, 0x75, 0x0A, 0x83, 0x7D, 0xE4, 0x00, 0xC6, 0x45, 0x0B, 0x01};
    set.expect(LONE_UNIT_FLAG, loneUnitFlag);
    // a battalion leaving: remember it in [edi + 0x20]
    const BYTE rememberBattalion[24] = {
        0x8B, 0x43, 0x04, 0xF6, 0x80, 0x15, 0x01, 0x00, 0x00, 0x20, 0x74, 0x42, 0x8B, 0x07, 0x6A, 0x01, 0x8D, 0x4D, 0x98, 0x51,
        0x8B, 0xCF, 0xFF, 0x50
    };
    set.expect(REMEMBER_BATTALION, rememberBattalion);
    // ... / mov [edi + 0x20], eax: the battalion's id
    const BYTE rememberBattalionStore[14] = {0xDF, 0xFF, 0x8B, 0x43, 0x74, 0x6A, 0x01, 0x6A, 0x03, 0x8B, 0xCB, 0x89, 0x47, 0x20};
    set.expect(REMEMBER_BATTALION + 0x30, rememberBattalionStore);

    // cmp dword [esp + 4], 0 / je
    const BYTE findObject[7] = {0x83, 0x7C, 0x24, 0x04, 0x00, 0x74, 0x14};
    set.expect(FIND_OBJECT_BY_ID, findObject);
    // lea eax, [ecx - 0x20] / add ecx, 0xFC
    const BYTE battalionInterfaceHead[9] = {0x8D, 0x41, 0xE0, 0x81, 0xC1, 0xFC, 0x00, 0x00, 0x00};
    set.expect(BATTALION_INTERFACE, battalionInterfaceHead);
    // the slot assignment the walk repeats: slots built at + 0x7C, built through [module + 0x84](1), free slots at + 0x78,
    // slot array at + 0x6C with 0x1C byte slots, payload lookup on the module data, the template by name, equivalence
    const BYTE assignSlot[24] = {
        0x55, 0x8B, 0xEC, 0x51, 0x51, 0x53, 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x7C, 0x00, 0x57, 0x75, 0x10, 0x8D, 0x8E, 0xE4, 0xFE,
        0xFF, 0xFF, 0x8B, 0x01
    };
    set.expect(ASSIGN_SLOT, assignSlot);
    const BYTE assignSlotWalk[24] = {
        0x6A, 0x01, 0xFF, 0x90, 0x84, 0x00, 0x00, 0x00, 0x8B, 0x46, 0x78, 0x8B, 0x38, 0x3B, 0xF8, 0x0F, 0x84, 0x84, 0x00, 0x00,
        0x00, 0x8B, 0x5D, 0x08
    };
    set.expect(ASSIGN_SLOT + 24, assignSlotWalk);
    const BYTE assignSlotPayload[24] = {
        0x8B, 0x47, 0x08, 0x8B, 0x4E, 0x6C, 0x89, 0x45, 0xFC, 0x6B, 0xC0, 0x1C, 0xFF, 0x34, 0x08, 0x8B, 0x8E, 0xE8, 0xFE, 0xFF,
        0xFF, 0xE8, 0x6D, 0x84
    };
    set.expect(ASSIGN_SLOT + 48, assignSlotPayload);
    const BYTE assignSlotTemplate[16] = {0xFF, 0xFF, 0x85, 0xC0, 0x74, 0x1C, 0x8B, 0x0D, 0x40, 0x4A, 0xDE, 0x00, 0x83, 0xC0, 0x04, 0x50};
    set.expect(ASSIGN_SLOT + 72, assignSlotTemplate);

    const BYTE payloadLookup[10] = {0x8B, 0x91, 0x8C, 0x01, 0x00, 0x00, 0x8B, 0x89, 0x90, 0x01};
    set.expect(PAYLOAD_LOOKUP, payloadLookup);
    const BYTE findTemplate[8] = {0x55, 0x8B, 0xEC, 0x51, 0x51, 0xFF, 0x75, 0x08};
    set.expect(FIND_TEMPLATE, findTemplate);
    const BYTE isEquivalent[8] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x56};
    set.expect(IS_EQUIVALENT, isEquivalent);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
