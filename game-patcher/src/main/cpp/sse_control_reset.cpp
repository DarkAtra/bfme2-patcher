#include <windows.h>
#include <cstdint>
#include <cstring>

#include "patch_set.h"
#include "sse_control_reset.h"

// SSE control reset
//
// Problem: the game's logic computes in SSE single precision (tens of thousands of addss / mulss / cvtsi2ss), and so do
// several patches (sse_math's sqrt, the crowd cost, closed gates, tunnel routing). Their results depend on the SSE control
// register MXCSR: rounding mode, flush to zero, denormals are zero. The game never sets it (no ldmxcsr in the exe, and
// msvcr71's _controlfp and _fpreset only touch the x87 control word), so a driver or injected DLL that changes it on the
// game's thread makes that machine compute differently from the others in a match.
// Fix: setFPMode (0x00440809), which the game calls at 20 places (once every logic frame, 0x0062E6A4) to reset the x87
// control word, also loads the Windows default MXCSR 0x1F80: every exception masked, round to nearest, no flush to zero, no
// denormals are zero. On a machine where nothing changed MXCSR, this changes nothing.

using namespace patching;

namespace {

    constexpr uintptr_t SET_FP_MODE = 0x00440809;
    constexpr uintptr_t SET_FP_MODE_REST = 0x0044080F;
    constexpr uintptr_t FPRESET_IMPORT = 0x00BD0590;
    constexpr uint32_t DEFAULT_MXCSR = 0x1F80;
    constexpr size_t CODE_OFFSET = 0x10;
}

bool applySseControlReset() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    memcpy(caveMemory, &DEFAULT_MXCSR, sizeof(DEFAULT_MXCSR));
    Cave cave(caveMemory);
    cave.size = CODE_OFFSET;
    const uintptr_t entry = cave.here();
    cave.emit8(0x0F); // ldmxcsr [DEFAULT_MXCSR]
    cave.emit8(0xAE);
    cave.emit8(0x15);
    cave.emit32(address(caveMemory));
    cave.callThrough(FPRESET_IMPORT); // the instruction the hook replaced
    cave.jmp(SET_FP_MODE_REST);

    static PatchSet set;
    set.reset();

    // call [_fpreset]
    const BYTE fpreset[6] = {0xFF, 0x15, 0x90, 0x05, 0xBD, 0x00};
    set.hook(SET_FP_MODE, fpreset, entry);
    // call [_statusfp] / and eax, ~0x10300 / or eax, 0x20000 / push 0x30300 / push eax / call [_controlfp] / pop ecx / pop ecx /
    // ret: the rest of setFPMode
    const BYTE rest[31] = {
        0xFF, 0x15, 0x94, 0x05, 0xBD, 0x00, 0x25, 0xFF, 0xFC, 0xFE, 0xFF, 0x0D, 0x00, 0x00, 0x02, 0x00, 0x68, 0x00, 0x03, 0x03,
        0x00, 0x50, 0xFF, 0x15, 0x98, 0x05, 0xBD, 0x00, 0x59, 0x59, 0xC3
    };
    set.expectBytes(SET_FP_MODE_REST, rest);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
