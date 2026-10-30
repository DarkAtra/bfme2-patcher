#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "siege_wall_teleport_fix.h"

// Siege wall teleport fix
//
// Problem: a siege engine pushed against a wall is lifted onto it.
// Fix: a siege engine is not put onto a wall's layer when the wall's top is more than a step above it. Siege engines that
// climb a ramp onto the wall still get there.
//
// A wall stamps its layer (17 .. 64) into every pathfinding cell its bounds cover, and three places put a moving object onto
// the layer of the cell under its centre: Pathfinder::updateObjectLayer (0x006F0741) when that layer's surface is more than
// 10 units above the object, and the moved-object and physics paths (0x0062E15F, 0x0079792F) with no height test at all.
// A wide siege engine gets its centre into a stamped cell at ground level and is lifted a storey. A unit that climbs a ramp
// arrives level with the wall top (measured by pySAGE: 0 .. 3.4 units), a pushed one a storey below it (14.7 .. 53.4). The
// three Object::setLayer calls go through a gate that refuses a wall layer whose surface (Pathfinder + 0x1BE78 + layer * 4)
// is more than 10 units above a MACHINE (KindOf 11). Infantry can still be lifted by the first path (pySAGE measured 14.7);
// whether ladders, siege towers and wall climbing heroes use these paths is unknown, so they are left alone. Derived from
// pySAGE's wall-layer-promotion (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t UPDATE_OBJECT_LAYER = 0x006F0741;
    constexpr uintptr_t PROMOTION_SET_LAYER = 0x006F07DA;
    constexpr uintptr_t MOVED_SET_LAYER = 0x0062E15C;
    constexpr uintptr_t PHYSICS_SET_LAYER = 0x0079792C;
    constexpr uintptr_t LAYER_FOR_DESTINATION = 0x00680A75;
    constexpr uintptr_t SET_LAYER = 0x0068BB9D;
    constexpr uintptr_t LAYER_HEIGHT_TABLE_READ = 0x006F0BA6;
    constexpr uintptr_t PHYSICS_IMMOBILE_TEST = 0x00797916;
    constexpr uintptr_t PHYSICS_PATHFINDER_READ = 0x00797940;
    constexpr uintptr_t FLOAT_TEN = 0x00BD83D8;

    constexpr BYTE FIRST_WALL_LAYER = 0x11;
    constexpr BYTE LAST_WALL_LAYER = 0x40;

    void putShortJump(BYTE *cave, const size_t jumpOffset, const size_t target) {
        cave[jumpOffset + 1] = static_cast<BYTE>(target - (jumpOffset + 2));
    }

    void addSetLayerCall(PatchSet &set, const uintptr_t arguments, const BYTE (&stockCall)[5], const uintptr_t gate) {
        // push eax / mov ecx, esi: the layer and the object
        const BYTE layerAndObject[3] = {0x50, 0x8B, 0xCE};
        set.expect(arguments, layerAndObject);
        set.redirectCall(arguments + 3, stockCall, gate);
    }
}

bool applySiegeWallTeleportFix() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // Entered by a call in place of setLayer: ecx is the object, [esp + 4] the layer. Only eax is changed before setLayer runs,
    // which changes it itself.
    Cave cave(caveMemory);
    const BYTE wallLayer[14] = {
        0x8B, 0x44, 0x24, 0x04, // mov eax, [esp + 4]
        0x83, 0xF8, FIRST_WALL_LAYER, // cmp eax, 0x11
        0x7C, 0x00, // jl allow
        0x83, 0xF8, LAST_WALL_LAYER, // cmp eax, 0x40
        0x7F, 0x00 // jg allow
    };
    const size_t belowWalls = cave.size + 7;
    const size_t aboveWalls = cave.size + 12;
    cave.emit(wallLayer);

    const BYTE save[8] = {
        0x52, // push edx
        0x83, 0xEC, 0x10, // sub esp, 0x10
        0x0F, 0x11, 0x04, 0x24 // movups [esp], xmm0
    };
    cave.emit(save);

    const BYTE machine[14] = {
        0x8B, 0x51, 0x04, // mov edx, [ecx + 4]: the template
        0x85, 0xD2, // test edx, edx
        0x74, 0x00, // jz restoreAndAllow
        0xF6, 0x82, 0x09, 0x01, 0x00, 0x00, 0x08, // test byte [edx + 0x109], 0x08: KindOf MACHINE
    };
    const size_t noTemplate = cave.size + 5;
    cave.emit(machine);
    const size_t notMachine = cave.size;
    const BYTE notMachineJump[2] = {0x74, 0x00}; // jz restoreAndAllow
    cave.emit(notMachineJump);

    const BYTE pathfinder[15] = {
        0x8B, 0x15, 0x40, 0x4B, 0xDE, 0x00, // mov edx, [0x00DE4B40]: TheAI
        0x85, 0xD2, // test edx, edx
        0x74, 0x00, // jz restoreAndAllow
        0x8B, 0x52, 0x10, // mov edx, [edx + 0x10]: the pathfinder
        0x85, 0xD2 // test edx, edx
    };
    const size_t noAi = cave.size + 8;
    cave.emit(pathfinder);
    const size_t noPathfinder = cave.size;
    const BYTE noPathfinderJump[2] = {0x74, 0x00}; // jz restoreAndAllow
    cave.emit(noPathfinderJump);

    const BYTE height[21] = {
        0xF3, 0x0F, 0x10, 0x84, 0x82, 0x78, 0xBE, 0x01, 0x00, // movss xmm0, [edx + eax * 4 + 0x1BE78]: the wall's top
        0xF3, 0x0F, 0x5C, 0x41, 0x40, // subss xmm0, [ecx + 0x40]: the object's height
        0x0F, 0x2F, 0x05, 0xD8, 0x83, 0xBD, 0x00 // comiss xmm0, [FLOAT_TEN]
    };
    cave.emit(height);
    const size_t storeyAbove = cave.size;
    const BYTE storeyAboveJump[2] = {0x77, 0x00}; // ja restoreAndRefuse
    cave.emit(storeyAboveJump);

    const BYTE restore[8] = {
        0x0F, 0x10, 0x04, 0x24, // movups xmm0, [esp]
        0x83, 0xC4, 0x10, // add esp, 0x10
        0x5A // pop edx
    };
    const size_t restoreAndAllow = cave.size;
    cave.emit(restore);
    const size_t allow = cave.size;
    cave.jmp(SET_LAYER);
    const size_t restoreAndRefuse = cave.size;
    cave.emit(restore);
    const BYTE refuse[3] = {0xC2, 0x04, 0x00}; // ret 4
    cave.emit(refuse);

    putShortJump(caveMemory, belowWalls, allow);
    putShortJump(caveMemory, aboveWalls, allow);
    putShortJump(caveMemory, noTemplate, restoreAndAllow);
    putShortJump(caveMemory, notMachine, restoreAndAllow);
    putShortJump(caveMemory, noAi, restoreAndAllow);
    putShortJump(caveMemory, noPathfinder, restoreAndAllow);
    putShortJump(caveMemory, storeyAbove, restoreAndRefuse);

    const auto gate = reinterpret_cast<uintptr_t>(caveMemory);

    static PatchSet set;
    set.reset();

    // push ebp / mov ebp, esp / sub esp, 0x10
    const BYTE updateObjectLayer[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};
    set.expect(UPDATE_OBJECT_LAYER, updateObjectLayer);

    // call SET_LAYER, from each of the three paths
    const BYTE promotionCall[5] = {0xE8, 0xBB, 0xB3, 0xF9, 0xFF};
    addSetLayerCall(set, PROMOTION_SET_LAYER, promotionCall, gate);
    const BYTE movedCall[5] = {0xE8, 0x39, 0xDA, 0x05, 0x00};
    addSetLayerCall(set, MOVED_SET_LAYER, movedCall, gate);
    const BYTE physicsCall[5] = {0xE8, 0x69, 0x42, 0xEF, 0xFF};
    addSetLayerCall(set, PHYSICS_SET_LAYER, physicsCall, gate);

    // push ebp / mov ebp, esp / push ecx / push ecx: the layer the two other paths set
    const BYTE layerForDestination[5] = {0x55, 0x8B, 0xEC, 0x51, 0x51};
    set.expect(LAYER_FOR_DESTINATION, layerForDestination);

    // push esi / mov esi, ecx / mov eax, [esi + 0x428]: the object's layer
    const BYTE setLayer[9] = {0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x28, 0x04, 0x00, 0x00};
    set.expect(SET_LAYER, setLayer);

    // movss xmm0, [edi + esi * 4 + 0x1BE78]: the pathfinder's own read of a wall layer's top
    const BYTE layerHeightTableRead[9] = {0xF3, 0x0F, 0x10, 0x84, 0xB7, 0x78, 0xBE, 0x01, 0x00};
    set.expect(LAYER_HEIGHT_TABLE_READ, layerHeightTableRead);

    // test byte [eax + 0x108], 4: KindOf IMMOBILE, which places the KindOf bits at template + 0x108
    const BYTE immobileTest[7] = {0xF6, 0x80, 0x08, 0x01, 0x00, 0x00, 0x04};
    set.expect(PHYSICS_IMMOBILE_TEST, immobileTest);

    // mov eax, [0x00DE4B40] / mov ecx, [eax + 0x10]: TheAI's pathfinder
    const BYTE pathfinderRead[8] = {0xA1, 0x40, 0x4B, 0xDE, 0x00, 0x8B, 0x48, 0x10};
    set.expect(PHYSICS_PATHFINDER_READ, pathfinderRead);

    const BYTE ten[4] = {0x00, 0x00, 0x20, 0x41};
    set.expect(FLOAT_TEN, ten);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
