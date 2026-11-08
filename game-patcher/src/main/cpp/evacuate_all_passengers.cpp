#include <windows.h>
#include <cstdint>

#include "evacuate_all_passengers.h"
#include "patch_set.h"

// Evacuate all passengers
//
// Problem: the EVACUATE button of a tunnel, garrison or transport leaves a passenger inside when it has a contain of its own
// that is not a battalion's: in 2.02 the Dwarven battle wagon in a mine shaft or on a ship, and Sharku on a ship.
// Fix: such passengers leave like every other unit, as their own exit button would send them.
//
// The exit-all loop (0x00991027) is the evacuate of the tunnel contain and four other contains (0x0087CEA0, in 0x0087CE8F,
// slot + 0x80 of vtable 0x00C5DCE0 and others) and of two transport contains (0x0087A56A, in 0x0087A559). It sends each
// passenger out by one of two arms: a passenger without a contain module of its own gets an exit order from its AI
// (0x00991086), a passenger whose contain is a battalion's sends the whole battalion out. A passenger with any other contain
// (a TransportContain, RiderChangeContain, CitadelSlaughterHordeContain, ...) is skipped. The hook sends such a passenger down
// the exit order arm instead. A passenger without an AI is still skipped, like one without a contain. pySAGE's
// evacuate-contained-heroes, which this derives from (https://github.com/ClementJ18/pySAGE), aims at mods whose Ring heroes
// carry a CitadelSlaughterHordeContain; no 2.02 unit does.

using namespace patching;

namespace {

    constexpr uintptr_t LOOP = 0x0099105E;
    constexpr uintptr_t BATTALION_TEST = 0x00991070;
    constexpr uintptr_t BATTALION_EXIT = 0x00991077;
    constexpr uintptr_t EXIT_ORDER = 0x00991086;
    constexpr uintptr_t EXIT_ORDER_REST = 0x0099108E;
    constexpr uintptr_t TRANSPORT_CALL = 0x0087A56A;
    constexpr uintptr_t TUNNEL_CALL = 0x0087CEA0;
}

bool applyEvacuateAllPassengers() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // eax is the passenger's battalion interface or null, esi the list node (+8: the passenger)
    Cave cave(caveMemory);
    const BYTE battalionTest[7] = {
        0x85, 0xC0, // test eax, eax
        0x74, 0x08, // je notBattalion
        0xFF, 0x75, 0x10 // push [ebp + 0x10] (the instruction the hook replaced)
    };
    cave.emit(battalionTest);
    cave.jmp(BATTALION_EXIT);
    const BYTE passenger[3] = {0x8B, 0x46, 0x08}; // notBattalion: mov eax, [esi + 8]
    cave.emit(passenger);
    cave.jmp(EXIT_ORDER);

    static PatchSet set;
    set.reset();

    // mov eax, [esi + 8] / mov ecx, [eax + 0x258] / test ecx, ecx / je EXIT_ORDER / mov eax, [ecx] / call [eax + 0x7C]: the
    // passenger's own contain and its battalion interface
    const BYTE loop[18] = {0x8B, 0x46, 0x08, 0x8B, 0x88, 0x58, 0x02, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x1B, 0x8B, 0x01, 0xFF, 0x50, 0x7C};
    set.expect(LOOP, loop);

    // test eax, eax / je next passenger / push [ebp + 0x10]
    const BYTE test[7] = {0x85, 0xC0, 0x74, 0x2A, 0xFF, 0x75, 0x10};
    set.hook(BATTALION_TEST, test, reinterpret_cast<uintptr_t>(caveMemory));

    // the battalion arm, then the exit order arm: mov eax, [eax + 0x260] / test eax, eax
    const BYTE battalionExit[23] = {
        0x8B, 0x10, 0xFF, 0x75, 0x0C, 0x8B, 0xC8, 0xFF, 0x92, 0x84, 0x00, 0x00, 0x00, 0xEB, 0x18, 0x8B, 0x80, 0x60, 0x02, 0x00,
        0x00, 0x85, 0xC0
    };
    set.expect(BATTALION_EXIT, battalionExit);

    // je next passenger / push / lea ecx, [eax + 0x20] / push / call 0x007716C1 (exit) / next passenger
    const BYTE exitOrderRest[23] = {
        0x74, 0x0E, 0xFF, 0x75, 0x10, 0x8D, 0x48, 0x20, 0xFF, 0x75, 0x0C, 0xE8, 0x23, 0x06, 0xDE, 0xFF, 0x8B, 0x36, 0x3B, 0x75,
        0x08, 0x75, 0xB9
    };
    set.expect(EXIT_ORDER_REST, exitOrderRest);

    // call 0x00991027
    const BYTE transportCall[5] = {0xE8, 0xB8, 0x6A, 0x11, 0x00};
    set.expect(TRANSPORT_CALL, transportCall);
    const BYTE tunnelCall[5] = {0xE8, 0x82, 0x41, 0x11, 0x00};
    set.expect(TUNNEL_CALL, tunnelCall);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
