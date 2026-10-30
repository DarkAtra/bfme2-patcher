#include <windows.h>
#include <cstdint>
#include <cstring>

#include "construction_initial_health.h"
#include "patch_set.h"

// Construction initial health
//
// Problem: a structure starts its construction with 1 hit point, so a foundation is a free kill while its builder walks to it.
// Fix: construction starts at 10 % of the maximum health. The health ramp and the construction percent derived from health are
// re-mapped, so health still reaches its maximum exactly when construction completes, and build times do not change.
//
// Four places drive a new foundation to 1 hit point (0x0079541F, 0x00858975, 0x0088D59E, 0x008AD88E): they call the body's
// internalChangeHealth (vtable + 0x84) with 1 - health. Health then rises by maxHealth / frames per frame, either in the
// builder's build state (0x0088DEA8) or, without a builder, in GettingBuiltBehavior (0x00857FC1), and two places compute the
// construction percent back from the health ratio (0x00856800, 0x00858078). The start calls now set max(10 % of the maximum, 1),
// the two ramps add (90 % of maxHealth + 1) / frames, and the percent becomes max(0, (ratio - 10 %) / 90 %) * 100. The extra hit
// point is stock's margin (it starts at 1 and adds the whole maximum): the sum overshoots the maximum, which the body clamps, so
// float rounding cannot leave a finished structure just below full health. Damage taken during construction can still bring a
// structure below 10 %. Derived from pySAGE's construction-initial-health
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t START_SEQUENCES[4] = {0x00795400, 0x0085895A, 0x0088D581, 0x008AD866};
    constexpr uintptr_t START_CALLS[4] = {0x0079541F, 0x00858975, 0x0088D59E, 0x008AD88E};
    constexpr uintptr_t BUILDER_RAMP = 0x0088DEA1;
    constexpr uintptr_t SELF_BUILD_RAMP = 0x00857FAF;
    constexpr uintptr_t PERCENT_FROM_HEALTH[2] = {0x00856800, 0x00858078};
    constexpr uintptr_t FLOAT_ONE = 0x00BD1908;
    constexpr uintptr_t FLOAT_HUNDRED = 0x00BD88D8;

    constexpr double START_FRACTION = 0.10;

    struct CaveData {
        float fraction;
        float one;
        float span;
        float inverseSpan;
        float hundred;
    };

    uint32_t address(const void *p) {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    }

    void emitWithAddress(Cave &cave, const BYTE *opcode, const size_t length, const void *operand) {
        for (size_t i = 0; i < length; i++) {
            cave.emit8(opcode[i]);
        }
        cave.emit32(address(operand));
    }

    void redirectToCall(PatchSet &set, const uintptr_t site, const BYTE (&stock)[6], const uintptr_t target) {
        BYTE call[6] = {0xE8, 0, 0, 0, 0, 0x90};
        const auto displacement = static_cast<int32_t>(target - (site + 5));
        memcpy(call + 1, &displacement, sizeof(displacement));
        set.replace(site, stock, call);
    }

    void redirectToCall(PatchSet &set, const uintptr_t site, const BYTE (&stock)[9], const uintptr_t target) {
        BYTE call[9] = {0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
        const auto displacement = static_cast<int32_t>(target - (site + 5));
        memcpy(call + 1, &displacement, sizeof(displacement));
        set.replace(site, stock, call);
    }
}

bool applyConstructionInitialHealth() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    auto *data = reinterpret_cast<CaveData *>(caveMemory);
    data->fraction = static_cast<float>(START_FRACTION);
    data->one = 1.0f;
    data->span = static_cast<float>(1.0 - START_FRACTION);
    data->inverseSpan = static_cast<float>(1.0 / (1.0 - START_FRACTION));
    data->hundred = 100.0f;

    Cave cave(caveMemory);
    cave.size = sizeof(CaveData);

    // In place of internalChangeHealth(1 - health, null): ecx is the body, [esp + 4] the delta. Turns the delta into
    // max(fraction * maximum, 1) - health and continues to internalChangeHealth, which returns to the caller.
    const uintptr_t start = cave.here();
    const BYTE startMaximum[17] = {
        0x51, // push ecx
        0x83, 0xEC, 0x04, // sub esp, 4
        0x8B, 0x01, // mov eax, [ecx]
        0xFF, 0x50, 0x1C, // call [eax + 0x1C]: the maximum health
        0xD9, 0x1C, 0x24, // fstp dword [esp]
        0xF3, 0x0F, 0x10, 0x04, 0x24 // movss xmm0, [esp]
    };
    cave.emit(startMaximum);
    const BYTE mulssFraction[4] = {0xF3, 0x0F, 0x59, 0x05}; // mulss xmm0, [fraction]
    emitWithAddress(cave, mulssFraction, sizeof(mulssFraction), &data->fraction);
    const BYTE movssOne[4] = {0xF3, 0x0F, 0x10, 0x0D}; // movss xmm1, [one]
    emitWithAddress(cave, movssOne, sizeof(movssOne), &data->one);
    const BYTE startDelta[32] = {
        0xF3, 0x0F, 0x5F, 0xC1, // maxss xmm0, xmm1: at least one hit point
        0xF3, 0x0F, 0x5C, 0xC1, // subss xmm0, xmm1
        0xF3, 0x0F, 0x58, 0x44, 0x24, 0x0C, // addss xmm0, [esp + 0xC]: plus the stock delta 1 - health
        0xF3, 0x0F, 0x11, 0x44, 0x24, 0x0C, // movss [esp + 0xC], xmm0
        0x83, 0xC4, 0x04, // add esp, 4
        0x59, // pop ecx
        0x8B, 0x01, // mov eax, [ecx]
        0xFF, 0xA0, 0x84, 0x00, 0x00, 0x00 // jmp [eax + 0x84]: internalChangeHealth
    };
    cave.emit(startDelta);

    // In place of call getMaximumHealth / fdiv [ebp - 0x1C] in the builder's ramp: st(0) = (span * maximum + 1) / frames.
    const uintptr_t builderRamp = cave.here();
    const BYTE rampMaximum[5] = {0x8B, 0x01, 0xFF, 0x50, 0x1C}; // mov eax, [ecx] / call [eax + 0x1C]
    cave.emit(rampMaximum);
    const BYTE fmulSpan[2] = {0xD8, 0x0D}; // fmul dword [span]
    emitWithAddress(cave, fmulSpan, sizeof(fmulSpan), &data->span);
    const BYTE faddOne[2] = {0xD8, 0x05}; // fadd dword [one]: stock's one hit point margin
    emitWithAddress(cave, faddOne, sizeof(faddOne), &data->one);
    const BYTE rampFrames[4] = {0xD8, 0x75, 0xE4, 0xC3}; // fdiv dword [ebp - 0x1C] / ret
    cave.emit(rampFrames);

    // In place of call getMaximumHealth / fild [esi + 0x1C] in the self-build ramp: st(1) = span * maximum + 1, st(0) = frames.
    const uintptr_t selfBuildRamp = cave.here();
    cave.emit(rampMaximum);
    emitWithAddress(cave, fmulSpan, sizeof(fmulSpan), &data->span);
    emitWithAddress(cave, faddOne, sizeof(faddOne), &data->one);
    const BYTE selfBuildFrames[4] = {0xDB, 0x46, 0x1C, 0xC3}; // fild dword [esi + 0x1C] / ret
    cave.emit(selfBuildFrames);

    // In place of call getHealthRatio / fmul [100.0]: st(0) = max(0, (ratio - fraction) / span) * 100.
    const uintptr_t percent = cave.here();
    const BYTE ratio[17] = {
        0x51, // push ecx
        0x83, 0xEC, 0x04, // sub esp, 4
        0x8B, 0x01, // mov eax, [ecx]
        0xFF, 0x50, 0x14, // call [eax + 0x14]: the health ratio
        0xD9, 0x1C, 0x24, // fstp dword [esp]
        0xF3, 0x0F, 0x10, 0x04, 0x24 // movss xmm0, [esp]
    };
    cave.emit(ratio);
    const BYTE subssFraction[4] = {0xF3, 0x0F, 0x5C, 0x05}; // subss xmm0, [fraction]
    emitWithAddress(cave, subssFraction, sizeof(subssFraction), &data->fraction);
    const BYTE mulssInverseSpan[4] = {0xF3, 0x0F, 0x59, 0x05}; // mulss xmm0, [inverseSpan]
    emitWithAddress(cave, mulssInverseSpan, sizeof(mulssInverseSpan), &data->inverseSpan);
    const BYTE notBelowZero[7] = {
        0x0F, 0x57, 0xC9, // xorps xmm1, xmm1
        0xF3, 0x0F, 0x5F, 0xC1 // maxss xmm0, xmm1
    };
    cave.emit(notBelowZero);
    const BYTE mulssHundred[4] = {0xF3, 0x0F, 0x59, 0x05}; // mulss xmm0, [hundred]
    emitWithAddress(cave, mulssHundred, sizeof(mulssHundred), &data->hundred);
    const BYTE percentResult[13] = {
        0xF3, 0x0F, 0x11, 0x04, 0x24, // movss [esp], xmm0
        0xD9, 0x04, 0x24, // fld dword [esp]
        0x83, 0xC4, 0x04, // add esp, 4
        0x59, // pop ecx
        0xC3 // ret
    };
    cave.emit(percentResult);

    static PatchSet set;
    set.reset();

    // mov reg, [obj + 0x25C] (the body) / ... / call [vtable + 0x10] (health) / fsubr [FLOAT_ONE] / push ecx / ... / fstp [esp]
    const BYTE startSequence0[24] = {
        0x8B, 0x9E, 0x5C, 0x02, 0x00, 0x00, 0x85, 0xDB, 0x74, 0x1B, 0x8B, 0x3B, 0x6A, 0x00, 0x8B, 0xCB, 0xFF, 0x57, 0x10, 0xD8,
        0x2D, 0x08, 0x19, 0xBD
    };
    set.expect(START_SEQUENCES[0], startSequence0);
    const BYTE startSequence1[21] = {
        0x8B, 0x9E, 0x5C, 0x02, 0x00, 0x00, 0x8B, 0x3B, 0x6A, 0x00, 0x8B, 0xCB, 0xFF, 0x57, 0x10, 0xD8, 0x2D, 0x08, 0x19, 0xBD,
        0x00
    };
    set.expect(START_SEQUENCES[1], startSequence1);
    const BYTE startSequence2[22] = {
        0x8B, 0x8B, 0x5C, 0x02, 0x00, 0x00, 0x8B, 0x39, 0x6A, 0x00, 0x89, 0x4D, 0xF8, 0xFF, 0x57, 0x10, 0xD8, 0x2D, 0x08, 0x19,
        0xBD, 0x00
    };
    set.expect(START_SEQUENCES[2], startSequence2);
    const BYTE startSequence3[24] = {
        0x8B, 0x8E, 0x5C, 0x02, 0x00, 0x00, 0x0F, 0x57, 0xC0, 0xF3, 0x0F, 0x11, 0x86, 0x88, 0x02, 0x00, 0x00, 0x8B, 0x19, 0x6A,
        0x00, 0x89, 0x4D, 0x18
    };
    set.expect(START_SEQUENCES[3], startSequence3);
    const BYTE startSequence3Rest[13] = {0xFF, 0x53, 0x10, 0xD8, 0x2D, 0x08, 0x19, 0xBD, 0x00, 0x51, 0x8B, 0x4D, 0x18};
    set.expect(START_SEQUENCES[3] + 24, startSequence3Rest);

    // call [edi + 0x84] (call [ebx + 0x84] at the last site): internalChangeHealth(1 - health, null)
    const BYTE startCall[6] = {0xFF, 0x97, 0x84, 0x00, 0x00, 0x00};
    const BYTE startCallEbx[6] = {0xFF, 0x93, 0x84, 0x00, 0x00, 0x00};
    redirectToCall(set, START_CALLS[0], startCall, start);
    redirectToCall(set, START_CALLS[1], startCall, start);
    redirectToCall(set, START_CALLS[2], startCall, start);
    redirectToCall(set, START_CALLS[3], startCallEbx, start);
    // fstp [esp], right before each start call
    const BYTE storeDelta[3] = {0xD9, 0x1C, 0x24};
    for (const uintptr_t call : START_CALLS) {
        set.expect(call - 3, storeDelta);
    }

    // mov esi, [ecx] / push 0 / mov [ebp - 0x10], ecx: the body, then the step and internalChangeHealth
    const BYTE builderRampHead[7] = {0x8B, 0x31, 0x6A, 0x00, 0x89, 0x4D, 0xF0};
    set.expect(BUILDER_RAMP, builderRampHead);
    // call [esi + 0x1C] / fdiv [ebp - 0x1C]
    const BYTE builderRampStep[6] = {0xFF, 0x56, 0x1C, 0xD8, 0x75, 0xE4};
    redirectToCall(set, BUILDER_RAMP + 7, builderRampStep, builderRamp);
    const BYTE builderRampTail[13] = {0x51, 0x8B, 0x4D, 0xF0, 0xD9, 0x1C, 0x24, 0xFF, 0x96, 0x84, 0x00, 0x00, 0x00};
    set.expect(BUILDER_RAMP + 13, builderRampTail);

    // mov ebx, [edi + 0x25C] / test ebx, ebx / je / mov eax, [ebx] / mov ecx, ebx
    const BYTE selfBuildHead[18] = {0x8B, 0x9F, 0x5C, 0x02, 0x00, 0x00, 0x85, 0xDB, 0x0F, 0x84, 0xDA, 0x00, 0x00, 0x00, 0x8B, 0x03, 0x8B, 0xCB};
    set.expect(SELF_BUILD_RAMP, selfBuildHead);
    // call [eax + 0x1C] / fild [esi + 0x1C]
    const BYTE selfBuildStep[6] = {0xFF, 0x50, 0x1C, 0xDB, 0x46, 0x1C};
    redirectToCall(set, SELF_BUILD_RAMP + 18, selfBuildStep, selfBuildRamp);
    // mov eax, [esi + 0x1C] / test eax, eax / jge / fadd [unsigned fixup] / fdivp st(1)
    const BYTE selfBuildDivide[15] = {0x8B, 0x46, 0x1C, 0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05, 0x98, 0x86, 0xBD, 0x00, 0xDE, 0xF9};
    set.expect(SELF_BUILD_RAMP + 24, selfBuildDivide);

    // call [eax + 0x14] / fmul [FLOAT_HUNDRED], then fstp [obj + 0x288] (the construction percent)
    const BYTE percentStep[9] = {0xFF, 0x50, 0x14, 0xD8, 0x0D, 0xD8, 0x88, 0xBD, 0x00};
    const BYTE percentStore[6] = {0xD9, 0x9F, 0x88, 0x02, 0x00, 0x00};
    for (const uintptr_t site : PERCENT_FROM_HEALTH) {
        redirectToCall(set, site, percentStep, percent);
        set.expect(site + 9, percentStore);
    }
    // mov ecx, [edi + 0x25C] / mov eax, [ecx]
    const BYTE percentBody[8] = {0x8B, 0x8F, 0x5C, 0x02, 0x00, 0x00, 0x8B, 0x01};
    set.expect(PERCENT_FROM_HEALTH[0] - 8, percentBody);
    // mov eax, [ebx] / mov ecx, ebx
    const BYTE percentBodySelfBuild[4] = {0x8B, 0x03, 0x8B, 0xCB};
    set.expect(PERCENT_FROM_HEALTH[1] - 4, percentBodySelfBuild);

    const BYTE one[4] = {0x00, 0x00, 0x80, 0x3F};
    set.expect(FLOAT_ONE, one);
    const BYTE hundred[4] = {0x00, 0x00, 0xC8, 0x42};
    set.expect(FLOAT_HUNDRED, hundred);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
