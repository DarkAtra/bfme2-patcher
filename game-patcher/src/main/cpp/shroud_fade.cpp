#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "shroud_fade.h"

// Shroud fade
//
// Problem: the fog of war fades in and out by a whole number of steps per drawn frame, the milliseconds since the last frame
// times a rate, rounded down, and the rest of the milliseconds is lost. At 60 frames per second one of the two fades loses about
// a twentieth of its speed; above about 64 frames per second it stops moving altogether.
// Fix: keeps the fraction of a step that was rounded away and adds it to the next frame's, so both fades move at their rate at
// any frame rate.
//
// Both fades (0x00472CF2 and 0x00473AAB, run on every drawn frame) add the elapsed milliseconds to their time stamp
// (0x00DC7900 / 0x00DC7908) and then take ftol(elapsed * rate) as the frame's steps (rate 0.255 at 0x00BDC530, 0.064 at
// 0x00BDC544), capped at 255 / 128. Only the display changes.

using namespace patching;

namespace {

    constexpr uintptr_t FTOL = 0x00A3CFA4;
    constexpr BYTE JBE = 0x86;

    struct Fade {
        uintptr_t site;
        uintptr_t rate;
        BYTE cap;
    };

    constexpr Fade FADES[2] = {
        {0x00472D44, 0x00BDC530, 0xFF},
        {0x00473AFD, 0x00BDC544, 0x80},
    };

    float remainders[2] = {};

    void addFade(PatchSet &set, Cave &cave, const Fade &fade, float *remainder) {
        const uintptr_t entry = cave.here();
        const BYTE multiply[2] = {0xD8, 0x0D}; // fmul dword [rate]
        cave.emit(multiply);
        cave.emit32(static_cast<uint32_t>(fade.rate));
        const BYTE addRemainder[2] = {0xD8, 0x05}; // fadd dword [remainder]
        cave.emit(addRemainder);
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(remainder)));
        const BYTE copy[2] = {0xD9, 0xC0}; // fld st(0): ftol pops its operand
        cave.emit(copy);
        cave.call(FTOL);
        const BYTE steps[3] = {0x89, 0x45, 0xEC}; // mov [ebp - 0x14], eax
        cave.emit(steps);
        const BYTE subtractSteps[3] = {0xDA, 0x65, 0xEC}; // fisub dword [ebp - 0x14]
        cave.emit(subtractSteps);
        const BYTE keep[2] = {0xD9, 0x1D}; // fstp dword [remainder]
        cave.emit(keep);
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(remainder)));
        // more steps than the cap (a long pause, or ftol's overflow) keep nothing over
        const BYTE overCap[3] = {0x81, 0x7D, 0xEC}; // cmp dword [ebp - 0x14], cap
        cave.emit(overCap);
        cave.emit32(fade.cap);
        const size_t kept = forwardJcc(cave, JBE);
        const BYTE clear[2] = {0xC7, 0x05}; // mov dword [remainder], 0
        cave.emit(clear);
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(remainder)));
        cave.emit32(0);
        cave.resolveJcc(kept, cave.here());
        cave.jmp(fade.site + 14);

        // fmul dword [rate] / call ftol / mov [ebp - 0x14], eax
        BYTE stock[14] = {0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x89, 0x45, 0xEC};
        const auto rate = static_cast<uint32_t>(fade.rate);
        memcpy(stock + 2, &rate, sizeof(rate));
        const auto displacement = static_cast<int32_t>(FTOL - (fade.site + 11));
        memcpy(stock + 7, &displacement, sizeof(displacement));
        set.hook(fade.site, stock, entry);
        // mov eax, cap: the steps are capped behind it
        const BYTE capLoad[5] = {0xB8, fade.cap, 0x00, 0x00, 0x00};
        set.expect(fade.site + 14, capLoad);
    }
}

bool applyShroudFade() {
    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();
    for (size_t i = 0; i < 2; i++) {
        addFade(set, cave, FADES[i], &remainders[i]);
    }
    // the rates: 0.255 and 0.064
    const BYTE fastRate[4] = {0x5C, 0x8F, 0x82, 0x3E};
    set.expect(0x00BDC530, fastRate);
    const BYTE slowRate[4] = {0x6F, 0x12, 0x83, 0x3D};
    set.expect(0x00BDC544, slowRate);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
