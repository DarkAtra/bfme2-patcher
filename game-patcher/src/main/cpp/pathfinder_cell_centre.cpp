#include <windows.h>
#include <cstdint>

#include "pathfinder_cell_centre.h"
#include "patch_set.h"

// Pathfinder cell centre
//
// Problem: the pathfinder's crowd cost asks for the world position of the same cell centre once for every unit near the
// cell (four floor calls and a terrain height lookup each), up to 200,000 times in a slow frame.
// Fix: computes the position once per crowd cost call and reuses it. The results are the same.
//
// The pathfinder prices a step into a cell by counting the units around it (crowd cost, 0x006ED21E). For every unit it
// finds in the cell window it first asks 0x006ED049 for the world position of the cell's centre: the footprint radius of
// the moving unit, the cell to world conversion and a terrain height lookup, which is four floor calls plus the height
// interpolation or a line scan over the height map. The arguments are the same for every unit in the window (the moving
// unit, the cell and a constant flag) and only x and y of the result are read, so a search that meets 200,000 units in a
// slow frame computes the same two floats 200,000 times.
//
// This computes it once per call. The result lives in the x and y locals the call already fills, and the z local, which
// nothing reads, becomes the "computed" flag. A hook ahead of the loops clears the flag, a hook at the call site skips
// the call once the flag is set. The values are the ones the call would have produced: the function only reads the unit,
// the terrain and the height map, none of which change while the crowd cost is being computed.

using namespace patching;

namespace {

    constexpr uintptr_t CROWD_COST = 0x006ED21E;
    constexpr uintptr_t LOOP_SETUP = 0x006ED273;
    constexpr uintptr_t LOOP_SETUP_NEXT = 0x006ED27B;
    constexpr uintptr_t CELL_CENTER_CALL = 0x006ED37A;
    constexpr uintptr_t CELL_CENTER_RESUME = 0x006ED389;
    constexpr uintptr_t CELL_CENTER = 0x006ED049;
}

bool applyPathfinderCellCentre() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    // mov dword [ebp - 0x28], 0 / movss xmm0, [0x00BD19DC] (the instruction the hook replaces) / jmp LOOP_SETUP_NEXT
    const uintptr_t clearFlag = cave.here();
    const BYTE clear[7] = {0xC7, 0x45, 0xD8, 0x00, 0x00, 0x00, 0x00};
    cave.emit(clear);
    const BYTE movss[8] = {0xF3, 0x0F, 0x10, 0x05, 0xDC, 0x19, 0xBD, 0x00};
    cave.emit(movss);
    cave.jmp(LOOP_SETUP_NEXT);

    const uintptr_t lookup = cave.here();
    // cmp dword [ebp - 0x28], 0 / jne reuse
    const BYTE flagSet[4] = {0x83, 0x7D, 0xD8, 0x00};
    cave.emit(flagSet);
    const size_t reuseJcc = cave.size;
    cave.jcc(JNE, 0);

    // The original call with its arguments: push 1 / push [ebp + 0x10] / lea eax, [ebp - 0x30] / push edi / push eax /
    // call. The code at the resume point removes the four arguments.
    const BYTE push1[2] = {0x6A, 0x01};
    const BYTE pushCell[3] = {0xFF, 0x75, 0x10};
    const BYTE leaPosition[3] = {0x8D, 0x45, 0xD0};
    const BYTE pushUnit = 0x57;
    const BYTE pushOutput = 0x50;
    cave.emit(push1);
    cave.emit(pushCell);
    cave.emit(leaPosition);
    cave.emit8(pushUnit);
    cave.emit8(pushOutput);
    cave.call(CELL_CENTER);
    // mov dword [ebp - 0x28], 1
    const BYTE setFlag[7] = {0xC7, 0x45, 0xD8, 0x01, 0x00, 0x00, 0x00};
    cave.emit(setFlag);
    cave.jmp(CELL_CENTER_RESUME);

    cave.resolveJcc(reuseJcc, cave.here());
    // The resume point still removes four arguments, which are not on the stack on this path: sub esp, 0x10
    const BYTE reserveArguments[3] = {0x83, 0xEC, 0x10};
    cave.emit(reserveArguments);
    cave.jmp(CELL_CENTER_RESUME);

    static PatchSet set;
    set.reset();

    // push ebp / mov ebp, esp / sub esp, 0x30
    const BYTE prologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30};
    set.expect(CROWD_COST, prologue);

    // movss xmm0, [0x00BD19DC], which both paths into the loops execute, then and dword [ebp + 0x14], 0
    const BYTE loopSetup[8] = {0xF3, 0x0F, 0x10, 0x05, 0xDC, 0x19, 0xBD, 0x00};
    set.hook(LOOP_SETUP, loopSetup, clearFlag);
    const BYTE loopSetupNext[4] = {0x83, 0x65, 0x14, 0x00};
    set.expect(LOOP_SETUP_NEXT, loopSetupNext);

    // push 1 / push [ebp + 0x10] / lea eax, [ebp - 0x30] / push edi / push eax / call 0x006ED049
    const BYTE call[15] = {0x6A, 0x01, 0xFF, 0x75, 0x10, 0x8D, 0x45, 0xD0, 0x57, 0x50, 0xE8, 0xC0, 0xFC, 0xFF, 0xFF};
    set.hook(CELL_CENTER_CALL, call, lookup);

    // xorps xmm0, xmm0 / add esp, 0x10
    const BYTE resume[6] = {0x0F, 0x57, 0xC0, 0x83, 0xC4, 0x10};
    set.expect(CELL_CENTER_RESUME, resume);

    // The function must still start at the address the hooks assume.
    const BYTE cellCenterStart[6] = {0x55, 0x8B, 0xEC, 0xFF, 0x75, 0x14};
    set.expect(CELL_CENTER, cellCenterStart);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
