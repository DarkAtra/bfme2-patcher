#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "production_ids.h"

// Game-wide production ids
//
// Problem: recruiting a hero at a second building while another hero is still being recruited takes the money and starts
// nothing.
// Fix: production ids are counted for the whole game instead of per building, so two recruits never share one.
//
// Every production order gets an id from requestUniqueUnitID (0x008A18FA), which counts per production module (+0x30,
// starting at 1), so every building hands out 1, 2, 3, ... A hero recruit is kept in the player's revive manager under that
// id, and startRevive (0x007812B2) refuses an id another recruit of the player already holds. queueCreateUnit (0x008A11D2)
// takes the money before it calls startRevive and drops the order without a refund when it refuses. The replacement counts
// in one place for the whole game and still starts at 1 (0 and -1 have meanings in the revive manager). The count starts
// over whenever a map is built (0x0062F91A: every match, replay and loaded save), so every player of a match and every
// replay of it hand out the same ids. A save does not keep the count: after loading a save made during a recruit, one
// recruit can still collide once. Derived from pySAGE's unique-production-id (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t REQUEST_UNIQUE_UNIT_ID = 0x008A18FA;
    // the production interface's vtable slot that names it; nothing else calls it
    constexpr uintptr_t REQUEST_UNIQUE_UNIT_ID_SLOT = 0x00C67DB8;
    constexpr uintptr_t BUILD_WORLD = 0x0062F91A;
    constexpr uintptr_t BUILD_WORLD_RESUME = 0x0062F91F;

    struct CaveData {
        uint32_t lastId;
    };
}

bool applyProductionIds() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    auto *data = reinterpret_cast<CaveData *>(caveMemory);
    data->lastId = 0;
    const auto lastId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&data->lastId));

    Cave cave(caveMemory);
    cave.size = sizeof(CaveData);

    const uintptr_t resetOnBuildWorld = cave.here();
    cave.emit8(0xC7); // mov dword [lastId], 0
    cave.emit8(0x05);
    cave.emit32(lastId);
    cave.emit32(0);
    const BYTE buildWorldHead[5] = {0xB8, 0xD4, 0x42, 0xB8, 0x00}; // mov eax, 0x00B842D4 (the exception frame's handler)
    cave.emit(buildWorldHead);
    cave.jmp(BUILD_WORLD_RESUME);

    static PatchSet set;
    set.reset();

    // mov eax, [ecx + 0x10] / lea edx, [eax + 1] / mov [ecx + 0x10], edx / ret
    const BYTE perBuilding[10] = {0x8B, 0x41, 0x10, 0x8D, 0x50, 0x01, 0x89, 0x51, 0x10, 0xC3};
    BYTE gameWide[10] = {0xB8, 0, 0, 0, 0, 0xFF, 0x00, 0x8B, 0x00, 0xC3}; // mov eax, lastId / inc [eax] / mov eax, [eax] / ret
    memcpy(gameWide + 1, &lastId, sizeof(lastId));
    set.replace(REQUEST_UNIQUE_UNIT_ID, perBuilding, gameWide);

    BYTE slot[4];
    putInt(slot, static_cast<int>(REQUEST_UNIQUE_UNIT_ID));
    set.expect(REQUEST_UNIQUE_UNIT_ID_SLOT, slot);

    set.hook(BUILD_WORLD, buildWorldHead, resetOnBuildWorld);

    // call: the exception frame's setup
    const BYTE buildWorldNext[5] = {0xE8, 0xCC, 0xD5, 0x40, 0x00};
    set.expect(BUILD_WORLD_RESUME, buildWorldNext);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
