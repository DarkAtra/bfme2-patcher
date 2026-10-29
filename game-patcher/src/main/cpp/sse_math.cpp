#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "sse_math.h"

// SSE math functions
//
// Problem: the game's floor, ceil, sqrt and fabs come from msvcr71, which switches the x87 control word around each call.
// The switches stall the CPU, and the pathfinder and the terrain lookups call these functions all the time.
// Fix: points the four imports at SSE2 versions that return bit identical results without touching the control word;
// arguments they do not handle exactly still go to the library.
//
// The game rounds through msvcr71's floor and ceil (415 and 144 call sites), most of them in the pathfinder, the terrain
// height lookup and the cell conversions. Both functions switch the x87 rounding mode with fldcw around frndint and
// switch it back, and fldcw stalls the pipeline. In the slow frames of a profiled mid game that is a fifth of the samples
// of the game thread.
//
// This replaces the import slots of the exe with SSE2 versions that compute the same result without touching the x87
// control word: truncate to int32, step by one when the truncation went the wrong way, convert back. The result is the
// exact integer the library returns, including the sign of a zero. Anything the int32 path cannot represent (NaN,
// infinities, magnitudes of 2^31 and above) is handed to the original function, so those keep the library's behaviour.
//
// sqrt (205 call sites, two per unit the pathfinder's crowd cost looks at) pays the same price: when the control word is
// not the 0x27F it wants, it loads 0x27F, takes the root with fsqrt and loads the old word back. A double precision fsqrt
// is correctly rounded, which is what sqrtsd computes, so the roots are the same bits. Negative arguments and NaN keep
// going through the library.
//
// fabs changes the control word too (about 100 ns a call). The replacement clears the sign bit, infinities and NaN keep going
// through the library.

using namespace patching;

namespace {

    constexpr uintptr_t FLOOR_IMPORT = 0x00BD0580;
    constexpr uintptr_t CEIL_IMPORT = 0x00BD0588;
    constexpr uintptr_t SQRT_IMPORT = 0x00BD06A0;
    constexpr uintptr_t FABS_IMPORT = 0x00BD06A8;

    // cvtsi2sd xmm1, eax
    void emitIntToDouble(Cave &cave) {
        const BYTE convert[4] = {0xF2, 0x0F, 0x2A, 0xC8};
        cave.emit(convert);
    }

    // sub esp, 8 / movsd [esp], xmm1 / fld qword [esp] / add esp, 8 / ret: the double in xmm1 is the result
    void emitReturnInSt0(Cave &cave) {
        const BYTE returnInSt0[14] = {0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x0C, 0x24, 0xDD, 0x04, 0x24, 0x83, 0xC4, 0x08};
        cave.emit(returnInSt0);
        cave.emit8(0xC3);
    }

    // Everything after the int32 result is in eax and the argument is still in xmm0: the zero case, the conversion back
    // and the return in st(0).
    void emitFinish(Cave &cave, const size_t fallbackJccOffsetCount, const size_t *fallbackJccOffsets, const uintptr_t fallbackSlot) {

        // test eax, eax
        const BYTE testResult[2] = {0x85, 0xC0};
        cave.emit(testResult);
        const size_t nonZeroJcc = cave.size;
        cave.jcc(JNE, 0);

        // The result is zero, which is -0.0 or +0.0 depending on the argument: keep only its sign bit.
        // psrlq xmm0, 63 / psllq xmm0, 63 / movapd xmm1, xmm0
        const BYTE signOnly[14] = {0x66, 0x0F, 0x73, 0xD0, 0x3F, 0x66, 0x0F, 0x73, 0xF0, 0x3F, 0x66, 0x0F, 0x28, 0xC8};
        cave.emit(signOnly);
        const size_t skipConversion = cave.size;
        cave.jmp(0);

        cave.resolveJcc(nonZeroJcc, cave.here());
        emitIntToDouble(cave);

        cave.resolveJmp(skipConversion, cave.here());
        emitReturnInSt0(cave);

        for (size_t i = 0; i < fallbackJccOffsetCount; i++) {
            cave.resolveJcc(fallbackJccOffsets[i], cave.here());
        }
        cave.jmpThrough(fallbackSlot);
    }

    // movsd xmm0, [esp + 4] / cvttsd2si eax, xmm0 / cmp eax, 0x80000000 / je fallback / cvtsi2sd xmm1, eax
    void emitTruncate(Cave &cave, size_t &fallbackJcc) {
        const BYTE load[6] = {0xF2, 0x0F, 0x10, 0x44, 0x24, 0x04};
        cave.emit(load);
        const BYTE truncate[4] = {0xF2, 0x0F, 0x2C, 0xC0};
        cave.emit(truncate);
        const BYTE outOfRange[5] = {0x3D, 0x00, 0x00, 0x00, 0x80};
        cave.emit(outOfRange);
        fallbackJcc = cave.size;
        cave.jcc(JE, 0);
        emitIntToDouble(cave);
    }
}

namespace sse_math {

    uintptr_t emitFloor(Cave &cave, const uintptr_t fallbackSlot) {

        const uintptr_t entry = cave.here();
        size_t fallbackJccs[1];
        emitTruncate(cave, fallbackJccs[0]);

        // floor is the truncation minus one when the argument is below it (negative, not an integer).
        // movapd xmm2, xmm0 / cmpltsd xmm2, xmm1 / movmskpd ecx, xmm2 / sub eax, ecx
        const BYTE step[13] = {0x66, 0x0F, 0x28, 0xD0, 0xF2, 0x0F, 0xC2, 0xD1, 0x01, 0x66, 0x0F, 0x50, 0xCA};
        cave.emit(step);
        const BYTE subtract[2] = {0x2B, 0xC1};
        cave.emit(subtract);

        emitFinish(cave, 1, fallbackJccs, fallbackSlot);
        return entry;
    }

    uintptr_t emitCeil(Cave &cave, const uintptr_t fallbackSlot) {

        const uintptr_t entry = cave.here();
        size_t fallbackJccs[2];
        emitTruncate(cave, fallbackJccs[0]);

        // ceil is the truncation plus one when the argument is above it (positive, not an integer), which overflows for
        // arguments just below 2^31.
        // movapd xmm2, xmm1 / cmpltsd xmm2, xmm0 / movmskpd ecx, xmm2 / add eax, ecx / jo fallback
        const BYTE step[13] = {0x66, 0x0F, 0x28, 0xD1, 0xF2, 0x0F, 0xC2, 0xD0, 0x01, 0x66, 0x0F, 0x50, 0xCA};
        cave.emit(step);
        const BYTE add[2] = {0x03, 0xC1};
        cave.emit(add);
        fallbackJccs[1] = cave.size;
        cave.jcc(JO, 0);

        emitFinish(cave, 2, fallbackJccs, fallbackSlot);
        return entry;
    }

    uintptr_t emitFabs(Cave &cave, const uintptr_t fallbackSlot) {

        const uintptr_t entry = cave.here();

        // mov eax, [esp + 8] (the high word of the argument) / mov edx, eax / and edx, 0x7FF00000 / cmp edx, 0x7FF00000 /
        // je fallback (an infinity or NaN)
        const BYTE exponent[16] = {0x8B, 0x44, 0x24, 0x08, 0x8B, 0xD0, 0x81, 0xE2, 0x00, 0x00, 0xF0, 0x7F, 0x81, 0xFA, 0x00, 0x00};
        cave.emit(exponent);
        const BYTE exponentEnd[2] = {0xF0, 0x7F};
        cave.emit(exponentEnd);
        const size_t fallbackJcc = cave.size;
        cave.jcc(JE, 0);

        // and eax, 0x7FFFFFFF / mov ecx, [esp + 4] / push eax / push ecx / fld qword [esp] / add esp, 8 / ret
        const BYTE clearSign[5] = {0x25, 0xFF, 0xFF, 0xFF, 0x7F};
        cave.emit(clearSign);
        const BYTE load[10] = {0x8B, 0x4C, 0x24, 0x04, 0x50, 0x51, 0xDD, 0x04, 0x24, 0x83};
        cave.emit(load);
        const BYTE loadEnd[3] = {0xC4, 0x08, 0xC3};
        cave.emit(loadEnd);

        cave.resolveJcc(fallbackJcc, cave.here());
        cave.jmpThrough(fallbackSlot);
        return entry;
    }

    uintptr_t emitSqrt(Cave &cave, const uintptr_t fallbackSlot) {

        const uintptr_t entry = cave.here();

        // movsd xmm0, [esp + 4] / pxor xmm1, xmm1 / comisd xmm0, xmm1 / jb fallback (carry is set for a negative number and
        // for NaN, not for -0.0, whose root is -0.0 as well)
        const BYTE load[6] = {0xF2, 0x0F, 0x10, 0x44, 0x24, 0x04};
        cave.emit(load);
        const BYTE compare[8] = {0x66, 0x0F, 0xEF, 0xC9, 0x66, 0x0F, 0x2F, 0xC1};
        cave.emit(compare);
        const size_t negativeJcc = cave.size;
        cave.jcc(JB, 0);

        // The library keeps the precision bits of the caller's control word, so the root comes out rounded to 24 or to 53
        // bits depending on them. sub esp, 4 / fnstcw [esp] / movzx eax, word [esp] / add esp, 4 / and eax, 0x300
        const BYTE readControlWord[11] = {0x83, 0xEC, 0x04, 0xD9, 0x3C, 0x24, 0x0F, 0xB7, 0x04, 0x24, 0x83};
        cave.emit(readControlWord);
        const BYTE readControlWordEnd[2] = {0xC4, 0x04};
        cave.emit(readControlWordEnd);
        const BYTE precisionBits[5] = {0x25, 0x00, 0x03, 0x00, 0x00};
        cave.emit(precisionBits);
        const size_t singleJcc = cave.size;
        cave.jcc(JE, 0);

        // 53 bits (0x200): cmp eax, 0x200 / jne fallback / sqrtsd xmm1, xmm0 / jmp finish
        const BYTE double53[5] = {0x3D, 0x00, 0x02, 0x00, 0x00};
        cave.emit(double53);
        const size_t otherPrecisionJcc = cave.size;
        cave.jcc(JNE, 0);
        const BYTE root[4] = {0xF2, 0x0F, 0x51, 0xC8};
        cave.emit(root);
        const size_t finishJmp = cave.size;
        cave.jmp(0);

        // 24 bits (0): an argument that is a float is rooted as a float, which rounds once to 24 bits like the x87 does.
        // cvtsd2ss xmm1, xmm0 / cvtss2sd xmm2, xmm1 / ucomisd xmm2, xmm0 / jne fallback / sqrtss xmm1, xmm1 / cvtss2sd xmm1, xmm1
        cave.resolveJcc(singleJcc, cave.here());
        const BYTE narrow[12] = {0xF2, 0x0F, 0x5A, 0xC8, 0xF3, 0x0F, 0x5A, 0xD1, 0x66, 0x0F, 0x2E, 0xD0};
        cave.emit(narrow);
        const size_t notAFloatJcc = cave.size;
        cave.jcc(JNE, 0);
        const BYTE rootSingle[8] = {0xF3, 0x0F, 0x51, 0xC9, 0xF3, 0x0F, 0x5A, 0xC9};
        cave.emit(rootSingle);

        cave.resolveJmp(finishJmp, cave.here());
        emitReturnInSt0(cave);

        cave.resolveJcc(negativeJcc, cave.here());
        cave.resolveJcc(otherPrecisionJcc, cave.here());
        cave.resolveJcc(notAFloatJcc, cave.here());
        cave.jmpThrough(fallbackSlot);
        return entry;
    }
}

bool applySseMath() {

    const HMODULE runtime = GetModuleHandleW(L"msvcr71.dll");
    if (runtime == nullptr) {
        return false;
    }

    struct Replacement {
        const char *name;
        uintptr_t importSlot;
        uintptr_t (*emit)(Cave &cave, uintptr_t fallbackSlot);
    };
    const Replacement replacements[] = {
        {"floor", FLOOR_IMPORT, sse_math::emitFloor},
        {"ceil", CEIL_IMPORT, sse_math::emitCeil},
        {"sqrt", SQRT_IMPORT, sse_math::emitSqrt},
        {"fabs", FABS_IMPORT, sse_math::emitFabs}
    };
    constexpr size_t COUNT = sizeof(replacements) / sizeof(replacements[0]);

    uintptr_t originals[COUNT];
    for (size_t i = 0; i < COUNT; i++) {
        originals[i] = reinterpret_cast<uintptr_t>(GetProcAddress(runtime, replacements[i].name));
        if (originals[i] == 0) {
            return false;
        }
    }

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();

    for (size_t i = 0; i < COUNT; i++) {
        // the original the fallback path jumps to
        const uintptr_t fallbackSlot = cave.here();
        cave.emit32(static_cast<uint32_t>(originals[i]));
        const uintptr_t replacement = replacements[i].emit(cave, fallbackSlot);

        // the slot must still hold the library function, otherwise something else already took it over
        BYTE expected[4];
        BYTE replaced[4];
        putInt(expected, static_cast<int>(originals[i]));
        putInt(replaced, static_cast<int>(replacement));
        set.replace(replacements[i].importSlot, expected, replaced);
    }

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
