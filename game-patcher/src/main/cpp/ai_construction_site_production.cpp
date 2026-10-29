#include <windows.h>
#include <cstdint>

#include "ai_construction_site_production.h"
#include "patch_set.h"

// AI production from construction sites
//
// Problem: the skirmish AI picks buildings that are still under construction to produce units and revive heroes, which the
// player cannot do. It pays at once and blocks the building's queue, and the unit cannot leave before the building is done.
// Fix: when the AI picks a building to use now, buildings under construction are skipped, so it uses a finished building or
// waits.
//
// The skirmish AI picks the building that is to produce a unit or revive a hero in 0x009A0705. It walks the buildings that can
// make the thing and takes the first one that is alive, has a production module, is not disabled and has an empty queue. It never
// asks whether the building is still under construction, and a building enters that list the moment it is placed. So the AI
// queues at construction sites: it pays at once and blocks the queue, and the unit cannot leave before the building is finished
// (ProductionUpdate holds it at the door, 0x008A1CB5), while a finished building would have delivered it on time. The player
// cannot do this, the control bar refuses under construction buildings.
//
// The picker's third argument selects between two questions: zero asks for a building to use now, anything else asks whether any
// building could ever make the thing (one caller cancels the AI's plan when the answer is none). Only the first question gets the
// test, so a plan is not dropped because its building is not finished yet. The hook replaces the branch that splits the two.
// Only the AI calls the picker. Derived from pySAGE's ai-construction-gate (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t PICKER = 0x009A0705;
    constexpr uintptr_t QUESTION_BRANCH = 0x009A0784;
    constexpr uintptr_t USABLE_TESTS = 0x009A078A;
    constexpr uintptr_t ACCEPT = 0x009A07A0;
    constexpr uintptr_t NEXT_CANDIDATE = 0x009A07C7;
    // thiscall(object, bit): whether the bit is set in the object's status bits (Object + 0x94)
    constexpr uintptr_t STATUS_TEST = 0x0044DDEC;

    constexpr BYTE UNDER_CONSTRUCTION = 2;
}

bool applyAiConstructionSiteProduction() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // edi is the candidate object, esi its production module. The status test keeps edi and esi; the code at USABLE_TESTS loads
    // eax and ecx itself.
    Cave cave(caveMemory);
    const BYTE whichQuestion[4] = {0x80, 0x7D, 0x10, 0x00}; // cmp byte [ebp + 0x10], 0
    cave.emit(whichQuestion);
    cave.jcc(JNE, ACCEPT);
    const BYTE testUnderConstruction[4] = {0x6A, UNDER_CONSTRUCTION, 0x8B, 0xCF}; // push 2 / mov ecx, edi
    cave.emit(testUnderConstruction);
    cave.call(STATUS_TEST);
    const BYTE testResult[2] = {0x84, 0xC0}; // test al, al
    cave.emit(testResult);
    cave.jcc(JNE, NEXT_CANDIDATE);
    cave.jmp(USABLE_TESTS);

    static PatchSet set;
    set.reset();

    // push ebp / mov ebp, esp / push ecx / push ecx / push ebx / push esi
    const BYTE pickerHead[7] = {0x55, 0x8B, 0xEC, 0x51, 0x51, 0x53, 0x56};
    set.expect(PICKER, pickerHead);

    // cmp byte [ebp + 0x10], 0 / jne ACCEPT
    const BYTE questionBranch[6] = {0x80, 0x7D, 0x10, 0x00, 0x75, 0x16};
    set.hook(QUESTION_BRANCH, questionBranch, reinterpret_cast<uintptr_t>(caveMemory));

    // mov eax, [esi] / mov ecx, esi / call [eax + 0x64]: whether the production module is disabled
    const BYTE usableTests[7] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x50, 0x64};
    set.expect(USABLE_TESTS, usableTests);

    // xor dl, dl / test ebx, ebx
    const BYTE accept[4] = {0x32, 0xD2, 0x85, 0xDB};
    set.expect(ACCEPT, accept);

    // push [ebp + 8] / call: the next candidate
    const BYTE nextCandidate[4] = {0xFF, 0x75, 0x08, 0xE8};
    set.expect(NEXT_CANDIDATE, nextCandidate);

    // mov edx, [esp + 4] / xor eax, eax / push esi / mov esi, ecx
    const BYTE statusTest[9] = {0x8B, 0x54, 0x24, 0x04, 0x33, 0xC0, 0x56, 0x8B, 0xF1};
    set.expect(STATUS_TEST, statusTest);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
