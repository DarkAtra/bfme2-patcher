#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "render_interpolation.h"

// Render interpolation
//
// Problem: moving units, turning and animations take a step twice as large as the others once per logic frame, about six
// times a second, at 30 and at 60 fps.
// Fix: the interpolation alpha is spread evenly over the sub-frames the client actually draws.
//
// Between two logic frames the client draws every object between its previous and its current transform, at the fraction
// alpha = sub-frame / ratio (GameEngine + 0x34 / + 0x38, stored at + 0x3C by 0x0063256F). The catch-up loop at 0x00632A9B
// steps the sub-frame counter past 1 inside the logic step that resets it, so the client only ever sees the sub-frames
// 2 .. ratio: alpha runs 2/ratio .. 1, and the first drawn frame of a logic frame moves 2/ratio instead of 1/ratio. The
// replacement takes alpha = (sub-frame - 1) / (ratio - 1), which steps by 1/(ratio - 1) everywhere and still ends at 1.
// The ratio is read at run time, so this works with and without the 60 fps patch. The alpha is only read by the client.
// Derived from pySAGE's interpolation-alpha (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t RECOMPUTE_ALPHA = 0x0063256F;
    constexpr uintptr_t CATCH_UP = 0x00632A9B;
    constexpr uintptr_t FLOAT_ONE = 0x00BD1908;
}

bool applyRenderInterpolation() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // ecx is the engine. Like the stock routine, the replacement changes only xmm0 and xmm1.
    Cave cave(caveMemory);
    const BYTE observedRange[20] = {
        0xF3, 0x0F, 0x2A, 0x49, 0x38, // cvtsi2ss xmm1, [ecx + 0x38]
        0xF3, 0x0F, 0x5C, 0x0D, 0x08, 0x19, 0xBD, 0x00, // subss xmm1, [FLOAT_ONE]
        0x0F, 0x57, 0xC0, // xorps xmm0, xmm0
        0x0F, 0x2F, 0xC8, // comiss xmm1, xmm0
        0x76 // jbe noRange (the ratio is 1 until the engine computes it first)
    };
    cave.emit(observedRange);
    const size_t noRangeJump = cave.size;
    cave.emit8(0x00);
    const BYTE alpha[42] = {
        0xF3, 0x0F, 0x2A, 0x41, 0x34, // cvtsi2ss xmm0, [ecx + 0x34]
        0xF3, 0x0F, 0x5C, 0x05, 0x08, 0x19, 0xBD, 0x00, // subss xmm0, [FLOAT_ONE]
        0xF3, 0x0F, 0x5E, 0xC1, // divss xmm0, xmm1
        0x0F, 0x57, 0xC9, // xorps xmm1, xmm1
        0xF3, 0x0F, 0x5F, 0xC1, // maxss xmm0, xmm1
        0xF3, 0x0F, 0x10, 0x0D, 0x08, 0x19, 0xBD, 0x00, // movss xmm1, [FLOAT_ONE]
        0xF3, 0x0F, 0x5D, 0xC1, // minss xmm0, xmm1
        0xF3, 0x0F, 0x11, 0x41, 0x3C, // movss [ecx + 0x3C], xmm0
        0xC3 // ret
    };
    cave.emit(alpha);
    caveMemory[noRangeJump] = static_cast<BYTE>(cave.size - (noRangeJump + 1));
    const BYTE noRange[14] = {
        0xF3, 0x0F, 0x10, 0x05, 0x08, 0x19, 0xBD, 0x00, // movss xmm0, [FLOAT_ONE]
        0xF3, 0x0F, 0x11, 0x41, 0x3C, // movss [ecx + 0x3C], xmm0
        0xC3 // ret
    };
    cave.emit(noRange);

    static PatchSet set;
    set.reset();

    // cvtsi2ss xmm1, [ecx + 0x38]
    const BYTE head[5] = {0xF3, 0x0F, 0x2A, 0x49, 0x38};
    set.hook(RECOMPUTE_ALPHA, head, reinterpret_cast<uintptr_t>(caveMemory));

    // the rest of the routine: sub-frame / ratio, clamped to [0, 1]
    const BYTE body[24] = {
        0xF3, 0x0F, 0x2A, 0x41, 0x34, 0xF3, 0x0F, 0x5E, 0xC1, 0x0F, 0x57, 0xC9, 0x0F, 0x2F, 0xC8, 0xF3, 0x0F, 0x11, 0x41, 0x3C,
        0x77, 0x0D, 0xF3, 0x0F
    };
    set.expect(RECOMPUTE_ALPHA + 5, body);
    const BYTE tail[20] = {
        0x10, 0x0D, 0x08, 0x19, 0xBD, 0x00, 0x0F, 0x2F, 0xC1, 0x76, 0x03, 0x0F, 0x28, 0xC1, 0xF3, 0x0F, 0x11, 0x41, 0x3C, 0xC3
    };
    set.expect(RECOMPUTE_ALPHA + 29, tail);

    // the catch-up loop that hides sub-frame 1: mov eax, 2 / jmp
    const BYTE catchUp[7] = {0xB8, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x19};
    set.expect(CATCH_UP, catchUp);

    const BYTE one[4] = {0x00, 0x00, 0x80, 0x3F};
    set.expect(FLOAT_ONE, one);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
