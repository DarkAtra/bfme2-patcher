#include <windows.h>
#include <cstdint>

#include "ai_rebuild_targets.h"
#include "patch_set.h"

// AI rebuild targets
//
// Problem: the skirmish AI casts Rebuild (and Snowbind and Untamed Allegiance, whose AI uses the same targeting) on
// buildings that are still under construction.
// Fix: the AI's target search skips buildings under construction.
//
// AISpecialPowerUpdate with SpecialPowerAIType = AI_SPELLBOOK_REBUILD builds a behaviour whose target picker (0x009E7744, slot
// 7 of the vtable at 0x00C8DEEC) walks the AI player's structure list and takes the first one below half health. A building
// under construction starts with 1 hit point, so the newest construction site always wins. The hook tests each candidate's
// UNDER_CONSTRUCTION status first and goes on to the next candidate when it is set. The stock loop has no test for a structure
// id that no longer resolves to an object and would crash on it; the hook skips such an id too.
// Derived from pySAGE's ai-rebuild-gate (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t FACTORY_CASE = 0x00992D50;
    constexpr uintptr_t FACTORY_CASE_SLOT = 0x00992F7A;
    constexpr uintptr_t FACTORY_CASE_CALL = 0x00992D6C;
    constexpr uintptr_t BEHAVIOUR_CONSTRUCTOR = 0x009E770B;
    constexpr uintptr_t PICKER_SLOT = 0x00C8DF08;
    constexpr uintptr_t PICKER = 0x009E7744;
    constexpr uintptr_t BODY_FETCH = 0x009E777C;
    constexpr uintptr_t HEALTH_TEST = 0x009E7782;
    constexpr uintptr_t NEXT_CANDIDATE = 0x009E7793;
    // thiscall(object, bit): whether the bit is set in the object's status bits (Object + 0x94)
    constexpr uintptr_t STATUS_TEST = 0x0044DDEC;

    constexpr BYTE UNDER_CONSTRUCTION = 2;
}

bool applyAiRebuildTargets() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // ebx is the candidate object; the status test changes eax, ecx and edx, which the loop loads before it uses them
    Cave cave(caveMemory);
    const BYTE objectTest[2] = {0x85, 0xDB}; // test ebx, ebx
    cave.emit(objectTest);
    cave.jcc(JE, NEXT_CANDIDATE);
    const BYTE testUnderConstruction[4] = {0x6A, UNDER_CONSTRUCTION, 0x8B, 0xCB}; // push 2 / mov ecx, ebx
    cave.emit(testUnderConstruction);
    cave.call(STATUS_TEST);
    const BYTE testResult[2] = {0x84, 0xC0}; // test al, al
    cave.emit(testResult);
    cave.jcc(JNE, NEXT_CANDIDATE);
    const BYTE bodyFetch[6] = {0x8B, 0x8B, 0x5C, 0x02, 0x00, 0x00}; // mov ecx, [ebx + 0x25C]: the body
    cave.emit(bodyFetch);
    cave.jmp(HEALTH_TEST);

    static PatchSet set;
    set.reset();

    // the type's factory case calls the constructor, which sets the vtable whose slot 7 is the picker
    BYTE caseAddress[4];
    putInt(caseAddress, static_cast<int>(FACTORY_CASE));
    set.expect(FACTORY_CASE_SLOT, caseAddress);
    const BYTE factoryCall[5] = {0xE8, 0x9A, 0x49, 0x05, 0x00};
    set.expect(FACTORY_CASE_CALL, factoryCall);
    // push esi / mov esi, ecx / call / mov dword [esi], 0x00C8DEEC
    const BYTE constructor[14] = {0x56, 0x8B, 0xF1, 0xE8, 0x58, 0xA5, 0x01, 0x00, 0xC7, 0x06, 0xEC, 0xDE, 0xC8, 0x00};
    set.expect(BEHAVIOUR_CONSTRUCTOR, constructor);
    BYTE pickerAddress[4];
    putInt(pickerAddress, static_cast<int>(PICKER));
    set.expect(PICKER_SLOT, pickerAddress);

    // push ebx / push ebp / push esi / mov ebp, ecx
    const BYTE picker[5] = {0x53, 0x55, 0x56, 0x8B, 0xE9};
    set.expect(PICKER, picker);

    // mov ecx, [ebx + 0x25C]
    const BYTE bodyFetchStock[6] = {0x8B, 0x8B, 0x5C, 0x02, 0x00, 0x00};
    set.hook(BODY_FETCH, bodyFetchStock, reinterpret_cast<uintptr_t>(caveMemory));

    // mov eax, [ecx] / call [eax + 0x14] (health ratio) / fld [0.5] / fcomip / fstp / ja take this one
    const BYTE healthTest[17] = {0x8B, 0x01, 0xFF, 0x50, 0x14, 0xD9, 0x05, 0x9C, 0x86, 0xBD, 0x00, 0xDF, 0xF1, 0xDD, 0xD8, 0x77, 0x11};
    set.expect(HEALTH_TEST, healthTest);

    // add esi, 4 / cmp esi, [edi + 4] / jne next
    const BYTE nextCandidate[8] = {0x83, 0xC6, 0x04, 0x3B, 0x77, 0x04, 0x75, 0xD2};
    set.expect(NEXT_CANDIDATE, nextCandidate);

    // mov edx, [esp + 4]
    const BYTE statusTest[4] = {0x8B, 0x54, 0x24, 0x04};
    set.expect(STATUS_TEST, statusTest);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
