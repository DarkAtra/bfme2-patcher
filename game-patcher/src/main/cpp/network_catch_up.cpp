#include <windows.h>
#include <cstdint>

#include "network_catch_up.h"
#include "patch_set.h"

// Network catch-up
//
// Problem: in a networked match the players who do not host trail the host by about 4 logic frames (0.8 s), and since the host
// puts every arriving command into its next frame, their commands take that much longer to execute (measured: 0.92 s against
// the host's 0.12 s). The lag comes from the match start (the host runs ahead while the others still load) and is kept for the
// whole match: an off-host peer cannot run faster than the host's pace, and EA's network slowdown (the frame limiter, 0x0063A10A)
// slows it down until 3 frames are buffered beyond the current one.
// Fix: an off-host peer that has n frames buffered beyond the current one runs the last n pairs of phases (11 + 12, 9 + 10,
// 7 + 8; at most 3) in one client frame each, checked when the pair's first phase has run, so the logic frame takes 12 - n client
// frames instead of 12 (up to 33 % faster, which also recovers below 60 fps). It never waits for that, since the frame it moves
// to is there. The slowdown no longer slows it down (its base 0.7 becomes 1.0), so it only waits when a
// packet is late, like the host. Every phase still runs once per logic frame and in order, through the game's own catch-up loop
// (0x00632ABB), so only the pace changes, never the logic. The host and games without a network are left alone.
//
// Compared with the 2.02 delay fix (the lotrbfme2ep1.fix build), which aims at the same lag:
// - 2.02 runs phases 1 and 2 in one client frame in every logic frame, for every player: 5 client frames per logic frame instead
//   of 6 at 30 fps. Single player, skirmish and replays run 20 % faster; the host, whose network allows a frame every 200 ms,
//   finishes early and holds about a client frame per logic frame; the off-host tries a frame every 165 ms and waits whenever
//   the host's frame is not there yet. Here only the off-host changes, only while frames are buffered (9 % faster with one,
//   up to 33 % with three or more), and it waits only for late packets.
// - 2.02 switches the slowdown off for everyone by accident: the limiter recomputes it on sub-frame 1 (0x0063A06E), which its
//   client never sees. Here it is switched off for the off-host on purpose.
// - 2.02 hides sub-frame 1 and needed a new divisor for the once per logic frame latch (0x0063252F); pairing later phases leaves
//   sub-frame 1 and the latch as they are.
// - Both are deterministic (every phase once per logic frame). Neither sends commands more than once per logic frame (about
//   0.1 s of the delay) nor changes the host putting an arriving command into its next frame.
//
// Sites: updateNetworkAndLogic (0x006329B0) decides after each phase whether its catch-up loop runs more phases in the same
// client frame (0x00632A9B; never at 60 client frames per 5 logic frames). The network (0x00DE4468), its connection manager
// (+ 0xC) with the local slot (+ 0x12028), the packet router's slot (+ 0x1202C) and the last frame it has every player's commands
// for (+ 0x1205C), the logic frame (TheGameLogic 0x00DE412C + 0x40), the wrap (the immediate of 0x0063264A).

using namespace patching;

namespace {

    constexpr uintptr_t THE_NETWORK = 0x00DE4468;
    constexpr uintptr_t THE_GAME_LOGIC = 0x00DE412C;
    constexpr uintptr_t WRAP_COMPARE = 0x0063264A;
    constexpr uintptr_t CATCH_UP_DECISION = 0x00632A9B;
    constexpr uintptr_t CATCH_UP_SHORT = 0x00632AA8;
    constexpr uintptr_t CATCH_UP_LOOP = 0x00632ABB;
    constexpr uintptr_t CATCH_UP_DONE = 0x00632B03;
    constexpr uintptr_t SLOWDOWN_BASE = 0x0063A123;

    // with n frames buffered beyond the current one, the last n pairs of phases (at most this many) share a client frame each
    constexpr int MAX_PAIRS = 3;

    // the off-host slowdown's target, min(1, 0.1 * buffered + base), stays at full speed: buffered is at least 0 after an advance
    const float slowdownBase = 1.0f;

    constexpr BYTE JGE = 0x8D;

    template<typename T>
    T read(const uintptr_t address) {
        return *reinterpret_cast<const T *>(address);
    }

    // The phase that the catch-up loop should run up to after `phase` in the same client frame, or 0 for none. Pair k (from 1)
    // starts at phase wrap + 1 - 2k and needs k buffered frames; phase 1 is never paired.
    int __cdecl catchUpTo(const int phase) {
        const int pair = (read<BYTE>(WRAP_COMPARE + 2) + 1 - phase) / 2;
        if (phase < 2 || pair < 1 || pair > MAX_PAIRS || phase != read<BYTE>(WRAP_COMPARE + 2) + 1 - 2 * pair) {
            return 0;
        }
        const auto network = read<uintptr_t>(THE_NETWORK);
        const auto logic = read<uintptr_t>(THE_GAME_LOGIC);
        const auto connections = network != 0 ? read<uintptr_t>(network + 0xC) : 0;
        if (connections == 0 || logic == 0 || read<int>(connections + 0x12028) == read<int>(connections + 0x1202C)) {
            return 0;
        }
        const int buffered = read<int>(connections + 0x1205C) - read<int>(logic + 0x40);
        return buffered >= pair ? phase + 1 : 0;
    }
}

bool applyNetworkCatchUp() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);

    // edi is the phase that just ran, eax:edx the client rate for the stock division
    const uintptr_t entry = cave.here();
    const BYTE callDecision[2] = {0x60, 0x57}; // pushad / push edi
    cave.emit(callDecision);
    cave.call(reinterpret_cast<uintptr_t>(&catchUpTo));
    const BYTE testDecision[5] = {0x83, 0xC4, 0x04, 0x85, 0xC0}; // add esp, 4 / test eax, eax
    cave.emit(testDecision);
    const size_t toCatchUp = cave.size;
    cave.jcc(JNE, 0);
    cave.emit8(0x61); // popad
    // the stock decision: idiv [logicRate] / mov ecx, eax / cmp ecx, 6 / jge past the loop
    const BYTE stockDecision[11] = {0xF7, 0x3D, 0x08, 0xF6, 0xD9, 0x00, 0x8B, 0xC8, 0x83, 0xF9, 0x06};
    cave.emit(stockDecision);
    cave.jcc(JGE, CATCH_UP_DONE);
    cave.jmp(CATCH_UP_SHORT);
    // the loop runs the phases after this one while they are below eax + 1
    cave.resolveJcc(toCatchUp, cave.here());
    const BYTE catchUp[5] = {0x89, 0x44, 0x24, 0x1C, 0x61}; // mov [esp + 0x1C], eax (eax after popad) / popad
    cave.emit(catchUp);
    cave.jmp(CATCH_UP_LOOP);

    static PatchSet set;
    set.reset();

    const BYTE decision[13] = {0xF7, 0x3D, 0x08, 0xF6, 0xD9, 0x00, 0x8B, 0xC8, 0x83, 0xF9, 0x06, 0x7D, 0x5B};
    set.hook(CATCH_UP_DECISION, decision, entry);
    // mov eax, [clientRate] / cdq in front of it
    const BYTE clientRate[6] = {0xA1, 0x0C, 0xF6, 0xD9, 0x00, 0x99};
    set.expect(CATCH_UP_DECISION - 6, clientRate);
    // edi = the phase argument, ebp = the engine; the phase's logic update: mov ecx, [TheGameLogic] / mov eax, [ecx] / push edi /
    // call [eax + 0x34]
    const BYTE phaseArgument[4] = {0x8B, 0x7C, 0x24, 0x14};
    set.expect(0x006329BF, phaseArgument);
    const BYTE engineInEbp[2] = {0x8B, 0xE9};
    set.expect(0x006329B8, engineInEbp);
    const BYTE phaseUpdate[12] = {0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x01, 0x57, 0xFF, 0x50, 0x34};
    set.expect(0x00632A79, phaseUpdate);
    // the loop: mov esi, eax / inc esi / jmp to its test; inc [ebp + 0x34] (the sub-frame), the time code, the logic update with the
    // next phase, inc edi / cmp edi, esi / jl; then the alpha and past the loop
    const BYTE loopHead[5] = {0x8B, 0xF0, 0x46, 0xEB, 0x29};
    set.expect(CATCH_UP_LOOP, loopHead);
    const BYTE nextSubFrame[3] = {0xFF, 0x45, 0x34};
    set.expect(0x00632AC0, nextSubFrame);
    set.expect(0x00632ADD, phaseUpdate);
    const BYTE loopTest[5] = {0x47, 0x3B, 0xFE, 0x7C, 0xD2};
    set.expect(0x00632AE9, loopTest);
    const BYTE alpha[9] = {0x8B, 0xCD, 0xE8, 0x7A, 0xFA, 0xFF, 0xFF, 0xEB, 0x0C};
    set.expect(0x00632AEE, alpha);
    const BYTE done[5] = {0x68, 0x50, 0xE3, 0xBF, 0x00};
    set.expect(CATCH_UP_DONE, done);
    const BYTE loopCount[3] = {0x8D, 0x47, 0xFF};
    set.expect(CATCH_UP_SHORT, loopCount);
    // cmp eax, wrap: the immediate is read at run time
    const BYTE wrapCompare[2] = {0x83, 0xF8};
    set.expect(WRAP_COMPARE, wrapCompare);
    // the globals read
    const BYTE loadNetwork[6] = {0x8B, 0x0D, 0x68, 0x44, 0xDE, 0x00};
    set.expect(0x006329D9, loadNetwork);
    const BYTE framesBehind[20] = {
        0x8B, 0x81, 0x28, 0x20, 0x01, 0x00, 0x3B, 0x81, 0x2C, 0x20, 0x01, 0x00, 0x74, 0x10, 0x8B, 0x81, 0x5C, 0x20, 0x01, 0x00
    };
    set.expect(0x008D4061, framesBehind);
    const BYTE logicFrame[9] = {0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x49, 0x40};
    set.expect(0x00632628, logicFrame);

    // the off-host slowdown: call [eax + 0x58] (frames buffered), cvtsi2ss, mulss [0.1], addss [base], then min with 1.0
    const BYTE buffered[3] = {0xFF, 0x50, 0x58};
    set.expect(0x0063A10C, buffered);
    const BYTE scaled[12] = {0xF3, 0x0F, 0x2A, 0xC0, 0xF3, 0x0F, 0x59, 0x05, 0xD4, 0x83, 0xBD, 0x00};
    set.expect(0x0063A117, scaled);
    const BYTE stockBase[8] = {0xF3, 0x0F, 0x58, 0x05, 0xA8, 0xE0, 0xBD, 0x00};
    BYTE base[8] = {0xF3, 0x0F, 0x58, 0x05};
    putInt(base + 4, static_cast<int>(reinterpret_cast<uintptr_t>(&slowdownBase)));
    set.replace(SLOWDOWN_BASE, stockBase, base);
    const BYTE minimum[5] = {0x0F, 0x2F, 0xC8, 0x77, 0x03};
    set.expect(0x0063A12B, minimum);
    const BYTE tenth[4] = {0xCD, 0xCC, 0xCC, 0x3D};
    set.expect(0x00BD83D4, tenth);
    const BYTE stockBaseValue[4] = {0x33, 0x33, 0x33, 0x3F};
    set.expect(0x00BDE0A8, stockBaseValue);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
