#include <windows.h>
#include <cstdint>

#include "instrumentation/fixed_seed.h"
#include "patch_set.h"

// Fixed seed (debug mode, with -fixedseed on the game's command line; launch-test.ps1 passes it with -AutoSkirmish)
//
// A skirmish seeds its random generators from the clock, so two runs of the auto skirmish play different matches (their logic
// CRCs differ from the first logged frame on). The seeding functions, InitRandom (0x006D321C, also the client's and the audio's
// generators) and InitGameLogicRandom (0x006D3261), take GlobalData + 0x1228 (the engine's fixed seed) instead of their argument
// when it is not -1: here they read FIXED_SEED instead, so every run plays the same match (measuring runs compare like with like,
// the logic CRC log compares builds).

using namespace patching;

namespace {

    constexpr uint32_t FIXED_SEED = 12345;
}

bool instrumentation::applyFixedSeed() {
    static PatchSet set;
    set.reset();

    // mov eax, [eax + 0x1228] (GlobalData's fixed seed) -> mov eax, FIXED_SEED / nop
    const BYTE fixedSeedRead[6] = {0x8B, 0x80, 0x28, 0x12, 0x00, 0x00};
    BYTE fixedSeed[6] = {0xB8, 0, 0, 0, 0, 0x90};
    putInt(fixedSeed + 1, FIXED_SEED);
    set.replace(0x006D3225, fixedSeedRead, fixedSeed);
    set.replace(0x006D326A, fixedSeedRead, fixedSeed);
    // cmp eax, -1 / mov edx, eax: the fixed seed taken when it is not -1
    const BYTE takeFixedSeed[5] = {0x83, 0xF8, 0xFF, 0x8B, 0xD0};
    set.expect(0x006D322B, takeFixedSeed);
    set.expect(0x006D3270, takeFixedSeed);

    return commit(set, Cave(nullptr));
}
