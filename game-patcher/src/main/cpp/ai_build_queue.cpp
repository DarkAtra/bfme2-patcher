#include <windows.h>
#include <cstdint>

#include "ai_build_queue.h"
#include "patch_set.h"

// AI build queue
//
// Problem: a skirmish AI build order that cannot get a builder (e.g. a farm with no plot left) stops the AI from starting
// any order behind it, units above all, so the AI banks money while its production buildings stand idle.
// Fix: such an order is skipped and the AI goes on to the next one.
//
// The AI keeps every build order in one list sorted by priority, and its order pump (0x008F0FB8) walks the list each tick
// and starts the first order that can start (0x00961975 answers 0). Answer 8 means the order has no builder it can use; the
// pump then ends the walk whenever the AI's pool of free builders is not empty. Farm orders have priority 2000, and a unit
// order only passes that while less than a third of the command points are used, so one farm order that cannot start keeps
// the AI from recruiting. Without the branch, answer 8 takes the pump's ordinary "not this one, next" path. Answer 2 (cannot
// afford it) still ends the walk, so the AI still saves for an order it cannot pay yet. Derived from pySAGE's
// ai-build-queue-skip (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t CAN_START_CALL = 0x008F1020;
    constexpr uintptr_t ANSWER_TEST = 0x008F1029;
    constexpr uintptr_t END_WALK_BRANCH = 0x008F1032;
}

bool applyAiBuildQueue() {

    static PatchSet set;
    set.reset();

    // call 0x00961975: can the order start now?
    const BYTE canStartCall[5] = {0xE8, 0x50, 0x09, 0x07, 0x00};
    set.expect(CAN_START_CALL, canStartCall);

    // cmp eax, 8 / jne / cmp dword [ebp - 8], 0: no builder, and how many builders the pool holds
    const BYTE answerTest[9] = {0x83, 0xF8, 0x08, 0x75, 0x06, 0x83, 0x7D, 0xF8, 0x00};
    set.expect(ANSWER_TEST, answerTest);

    // ja: end the walk
    const BYTE endWalk[2] = {0x77, 0x3F};
    const BYTE next[2] = {0x90, 0x90};
    set.replace(END_WALK_BRANCH, endWalk, next);

    return commit(set, Cave(nullptr));
}
