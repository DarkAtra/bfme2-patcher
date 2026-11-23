#include <windows.h>
#include <cstdint>

#include "features.h"
#include "patch_set.h"
#include "pathfinder_closest_cell.h"

// Pathfinder closest cell
//
// Problem: ordered into a crowd, most battalions get no path and drop the order; only some of the selected units move.
// Fix: when the path search cannot reach the goal, it falls back to the closest cell it reached where the unit fits, as the
// game intends, instead of giving up.
//
// The path search (InternalFindPath, 0x006FD06F) remembers the closest cell it has reached so far, for when the goal turns out to be
// unreachable or blocked: it then returns a path to that cell and sets the retry flag, so the unit moves as far as it can and plans
// again later. A cell becomes that fallback if it is closer than the best so far (by the hierarchical path's index, then by distance)
// and passes three checks: CheckDestination (0x006F3082), the zone test (0x006E8200) and the footprint scan (0x006EF3C3). The search
// moves its "best so far" to every closer cell before checking it, so in a crowd the occupied cells near the goal use up the record
// and the free cells a little farther away are never considered. With no fallback the search returns no path, the move state fails
// (0x00748EE4) and the unit goes idle. The game's other search with a fallback, FindClosestPath, checks first and only then moves its
// record; this makes InternalFindPath do the same.
//
// The three record stores before the checks (0x006FDA9D, 0x006FDAB5, 0x006FDB29) become nops, and the store of the fallback cell
// (0x006FDB78, where both branches meet) also stores the distance, which edi still holds, and with a hierarchical path the cell's
// index on it, computed again with the same call (0x0093AD93, two map lookups that change nothing). The checks only read the map and
// the units, so calling them for more cells changes nothing but time; the result depends only on the game's state, the same on every
// machine.
//
// With PATHFINDER_CLOSEST_CELL_ALLIES, a crowd of the unit's own army no longer leaves it without a fallback either. The footprint
// scan rejects every cell where an allied soldier stands or that an allied unit has as its goal, while the search itself walks
// through allies (they add cost and are asked to make way). So while the search has no fallback, a cell that fails the checks is
// checked again with allies ignored in the footprint scan (the first two checks, then the scan's tests without allied objects), and the
// closest such cell, by the same order, is kept as a second fallback. Where the search ends without a fallback (0x006FDDA9), it takes
// the second one: the unit moves as far towards the goal as the crowd allows, and the retry flag makes it plan again from there. The
// second fallback is reset where the search starts (0x006FD992). The relationship (0x0068D7AB) depends on the teams only.

using namespace patching;

namespace {

    constexpr uintptr_t INDEX_STORE = 0x006FDA9D;
    constexpr uintptr_t HIERARCHICAL_DISTANCE_STORE = 0x006FDAB5;
    constexpr uintptr_t FLAT_DISTANCE_STORE = 0x006FDB29;
    constexpr uintptr_t FALLBACK_STORE = 0x006FDB78;
    constexpr uintptr_t FALLBACK_STORE_RESUME = 0x006FDB7F;
    // thiscall(hierarchical path, cell info), ret 4: the cell's index on the hierarchical path
    constexpr uintptr_t PATH_INDEX = 0x0093AD93;

    constexpr uintptr_t SEARCH_START = 0x006FD992;
    constexpr uintptr_t SEARCH_START_RESUME = 0x006FD99B;
    constexpr uintptr_t NO_FALLBACK = 0x006FDDA9;
    constexpr uintptr_t NO_FALLBACK_RESUME = 0x006FDDB2;
    constexpr uintptr_t FALLBACK_PATH = 0x006FDD6D;
    constexpr uintptr_t SEARCH_CLEANUP = 0x006F5519;

    // the three checks: thiscall(pathfinder, mover, x, y, layer, radius, centred) CheckDestination, thiscall(pathfinder, movement info,
    // cell) the zone test, cdecl(position *out, centred, cell) the cell's centre the footprint scan is asked about
    constexpr uintptr_t CELL_CHECK = 0x006F3082;
    constexpr uintptr_t ZONE_CHECK = 0x006E8200;
    constexpr uintptr_t CELL_CENTRE = 0x006EA0A1;
    // the footprint scan's helpers (0x006EF3C3): cdecl(object, int *before, int *after), cdecl(cell index *out, object, position),
    // thiscall(terrain logic, object, position), thiscall(pathfinder, layer, x, y), thiscall(object)
    constexpr uintptr_t FOOTPRINT = 0x006ED0A0;
    constexpr uintptr_t CELL_OF = 0x006ECFDE;
    constexpr uintptr_t LAYER_OF = 0x00680A75;
    constexpr uintptr_t TERRAIN_LOGIC = 0x00DE4690;
    constexpr uintptr_t GET_CELL = 0x005E2E9C;
    constexpr uintptr_t FLAGGED_CELL_TEST = 0x0068B68A;
    // thiscall(object, other): 2 allies
    constexpr uintptr_t RELATIONSHIP = 0x0068D7AB;
    constexpr uint32_t ALLIES = 2;

    // the search's frame (ebp, 0x60 below the frame base)
    constexpr size_t FRAME_RADIUS = 0x2C;
    constexpr size_t FRAME_FALLBACK = 0x50;
    constexpr size_t FRAME_CENTRED = 0x54;
    constexpr size_t FRAME_MOVER = 0x68;
    constexpr size_t FRAME_HIERARCHICAL_PATH = 0x78;
    constexpr ptrdiff_t FRAME_MOVEMENT_INFO = -0x0C;

    constexpr size_t OBJECT_TEMPLATE = 0x04;
    constexpr size_t OBJECT_ID = 0x74;
    constexpr size_t OBJECT_CONTAINED_BY = 0x27C;
    constexpr size_t TEMPLATE_KINDS = 0x114;
    constexpr uint32_t KIND_HORDE = 0x2000;
    constexpr uint32_t KIND_CRUSHER = 0x20000000;
    constexpr size_t TEMPLATE_CRUSHABLE = 0x109;
    constexpr size_t TEMPLATE_GOAL_PASS = 0x117;
    constexpr size_t CELL_FLAGS = 0x0C;
    constexpr size_t INFO_UNITS = 0x20;
    constexpr size_t INFO_GOALS = 0x14;

    struct Fallback {
        uint8_t *cell;
        int32_t index;
        int32_t distance;
    };

    // the second fallback of the search running now; the pathfinder runs on the game's logic thread only
    Fallback allyFallback;

    struct CellIndex {
        int32_t x;
        int32_t y;
    };

    template<typename T>
    T read(const void *base, const ptrdiff_t offset) {
        return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
    }

    uint32_t kinds(const uint8_t *object) {
        return read<uint32_t>(read<const uint8_t *>(object, OBJECT_TEMPLATE), TEMPLATE_KINDS);
    }

    uint8_t templateByte(const uint8_t *object, const size_t offset) {
        return read<uint8_t>(read<const uint8_t *>(object, OBJECT_TEMPLATE), offset);
    }

    bool allied(uint8_t *mover, uint8_t *other) {
        return reinterpret_cast<uint32_t (__thiscall *)(uint8_t *, uint8_t *)>(RELATIONSHIP)(mover, other) == ALLIES;
    }

    // The footprint scan (0x006EF3C3) with allied objects ignored: the same cells, cell tests and object tests, in the same order.
    bool footprintFreeOfOthers(void *pathfinder, uint8_t *mover, const float *position) {
        int32_t before = 0;
        int32_t after = 0;
        reinterpret_cast<void (__cdecl *)(uint8_t *, int32_t *, int32_t *)>(FOOTPRINT)(mover, &before, &after);
        CellIndex cell = {};
        reinterpret_cast<void (__cdecl *)(CellIndex *, uint8_t *, const float *)>(CELL_OF)(&cell, mover, position);
        void *terrain = *reinterpret_cast<void **>(TERRAIN_LOGIC);
        const uint32_t layer = reinterpret_cast<uint32_t (__thiscall *)(void *, uint8_t *, const float *)>(LAYER_OF)(terrain, mover, position);
        const auto *container = read<const uint8_t *>(mover, OBJECT_CONTAINED_BY);
        const uint32_t excluded = container != nullptr ? read<uint32_t>(container, OBJECT_ID) : 0;
        const uint32_t moverKinds = kinds(mover);
        const auto getCell = reinterpret_cast<uint8_t *(__thiscall *)(void *, uint32_t, int32_t, int32_t)>(GET_CELL);
        const auto flaggedCellTest = reinterpret_cast<uint8_t (__thiscall *)(uint8_t *)>(FLAGGED_CELL_TEST);
        for (int32_t x = cell.x - before; x < cell.x + after; x++) {
            for (int32_t y = cell.y - before; y < cell.y + after; y++) {
                const uint8_t *pathCell = getCell(pathfinder, layer, x, y);
                if (pathCell == nullptr) {
                    return false;
                }
                const uint32_t flags = read<uint32_t>(pathCell, CELL_FLAGS);
                const uint32_t type = flags & 0xF;
                if (type == 5 || (((flags >> 18) & 1) != 0 && flaggedCellTest(mover) != 0) || type == 4 || type == 6) {
                    return false;
                }
                const auto *info = read<const uint8_t *>(pathCell, 0);
                if (info == nullptr) {
                    continue;
                }
                for (auto *node = read<const uint8_t *>(info, INFO_UNITS); node != nullptr; node = read<const uint8_t *>(node, 0)) {
                    auto *other = read<uint8_t *>(node, 8);
                    if (other == mover || read<uint32_t>(other, OBJECT_ID) == excluded) {
                        continue;
                    }
                    if ((moverKinds & KIND_HORDE) == 0 && (kinds(other) & KIND_HORDE) != 0) {
                        continue;
                    }
                    if ((moverKinds & KIND_CRUSHER) != 0 && (templateByte(other, TEMPLATE_CRUSHABLE) & 1) != 0) {
                        continue;
                    }
                    if (!allied(mover, other)) {
                        return false;
                    }
                }
                for (auto *node = read<const uint8_t *>(info, INFO_GOALS); node != nullptr; node = read<const uint8_t *>(node, 0)) {
                    auto *other = read<uint8_t *>(node, 8);
                    if (other == mover || ((kinds(other) & KIND_HORDE) != 0 && (moverKinds & KIND_HORDE) == 0)) {
                        continue;
                    }
                    if ((templateByte(other, TEMPLATE_CRUSHABLE) & 1) != 0 && (templateByte(mover, TEMPLATE_GOAL_PASS) & 0x20) != 0) {
                        continue;
                    }
                    if (!allied(mover, other)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // Called for a cell that failed the three checks while the search has no fallback: keeps it as the second fallback if it is
    // closer than the one kept and passes the checks with allies ignored.
    void __stdcall considerAllyFallback(uint8_t *frame, void *pathfinder, uint8_t *cell, const int32_t distance) {
        auto *path = read<void *>(frame, FRAME_HIERARCHICAL_PATH);
        int32_t index = 0;
        if (path != nullptr) {
            index = reinterpret_cast<int32_t (__thiscall *)(void *, void *)>(PATH_INDEX)(path, read<void *>(cell, 0));
            if (index < allyFallback.index || (index == allyFallback.index && distance >= allyFallback.distance)) {
                return;
            }
        } else if (distance >= allyFallback.distance) {
            return;
        }
        auto *mover = read<uint8_t *>(frame, FRAME_MOVER);
        const uint32_t centred = read<uint32_t>(frame, FRAME_CENTRED);
        const auto *cellInfo = read<const int32_t *>(cell, 0);
        const uint32_t layer = (read<uint32_t>(cell, CELL_FLAGS) >> 4) & 0x3F;
        const auto cellCheck = reinterpret_cast<uint8_t (__thiscall *)(void *, uint8_t *, int32_t, int32_t, uint32_t, uint32_t, uint32_t)>(CELL_CHECK);
        if (cellCheck(pathfinder, mover, cellInfo[0], cellInfo[1], layer, read<uint32_t>(frame, FRAME_RADIUS), centred) == 0) {
            return;
        }
        const auto zoneCheck = reinterpret_cast<uint8_t (__thiscall *)(void *, uint8_t *, uint8_t *)>(ZONE_CHECK);
        if (zoneCheck(pathfinder, frame + FRAME_MOVEMENT_INFO, cell) == 0) {
            return;
        }
        float centre[3];
        const auto *position = reinterpret_cast<const float *(__cdecl *)(float *, uint32_t, uint8_t *)>(CELL_CENTRE)(centre, centred, cell);
        if (!footprintFreeOfOthers(pathfinder, mover, position)) {
            return;
        }
        allyFallback = {cell, index, distance};
    }
}

bool applyPathfinderClosestCell() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    const bool allies = features::PATHFINDER_CLOSEST_CELL_ALLIES;

    // test al, al / je failed: the three checks' result
    const uintptr_t fallbackStore = cave.here();
    const BYTE testResult[2] = {0x84, 0xC0};
    cave.emit(testResult);
    const size_t failedJump = cave.size;
    const BYTE shortJe[2] = {0x74, 0x00};
    cave.emit(shortJe);
    // mov [ebp + 0x50], esi / mov [ebp + 0x34], edi: the cell and its distance
    const BYTE storeCell[3] = {0x89, 0x75, 0x50};
    cave.emit(storeCell);
    const BYTE storeDistance[3] = {0x89, 0x7D, 0x34};
    cave.emit(storeDistance);
    // cmp dword [ebp + 0x78], 0 / je resume: no hierarchical path
    const BYTE hierarchical[4] = {0x83, 0x7D, 0x78, 0x00};
    cave.emit(hierarchical);
    const size_t flatJump = cave.size;
    cave.emit(shortJe);
    // push [esi] / mov ecx, [ebp + 0x78] / call PATH_INDEX / mov [ebp + 0x24], eax: the cell's index on the path
    const BYTE pushCellInfo[2] = {0xFF, 0x36};
    cave.emit(pushCellInfo);
    const BYTE loadPath[3] = {0x8B, 0x4D, 0x78};
    cave.emit(loadPath);
    cave.call(PATH_INDEX);
    const BYTE storeIndex[3] = {0x89, 0x45, 0x24};
    cave.emit(storeIndex);
    const size_t resume = cave.size;
    cave.jmp(FALLBACK_STORE_RESUME);
    cave.resolveShortJump(flatJump, resume);
    if (allies) {
        // failed: cmp dword [ebp + 0x50], 0 / jne resume: only while the search has no fallback
        cave.resolveShortJump(failedJump, cave.size);
        const BYTE hasFallback[4] = {0x83, 0x7D, 0x50, 0x00};
        cave.emit(hasFallback);
        const size_t keptJump = cave.size;
        const BYTE shortJne[2] = {0x75, 0x00};
        cave.emit(shortJne);
        // pushad / push edi (distance) / push esi (cell) / push ebx (pathfinder) / push ebp (frame) / call / popad / jmp resume
        cave.emit8(0x60);
        cave.emit8(0x57);
        cave.emit8(0x56);
        cave.emit8(0x53);
        cave.emit8(0x55);
        cave.call(reinterpret_cast<uintptr_t>(&considerAllyFallback));
        cave.emit8(0x61);
        cave.jmp(FALLBACK_STORE_RESUME);
        cave.resolveShortJump(keptJump, resume);
    } else {
        cave.resolveShortJump(failedJump, resume);
    }

    // where the search starts: the second fallback is empty (mov dword [cell], 0 / mov dword [index], 0 / mov dword [distance],
    // 0x7FFFFFFF), then the replaced stores of the first one and jmp back
    const uintptr_t searchStart = cave.here();
    const BYTE storeImmediate[2] = {0xC7, 0x05};
    cave.emit(storeImmediate);
    cave.emit32(address(&allyFallback.cell));
    cave.emit32(0);
    cave.emit(storeImmediate);
    cave.emit32(address(&allyFallback.index));
    cave.emit32(0);
    cave.emit(storeImmediate);
    cave.emit32(address(&allyFallback.distance));
    cave.emit32(0x7FFFFFFF);
    const BYTE slotStores[9] = {0x89, 0x7D, 0x50, 0x89, 0x7D, 0x24, 0x89, 0x45, 0x34};
    cave.emit(slotStores);
    cave.jmp(SEARCH_START_RESUME);

    // no fallback at the search's end: mov eax, [cell] / test eax, eax / je stock / mov [ebp + 0x50], eax / jmp FALLBACK_PATH (ecx is
    // the pathfinder there as here); stock: mov byte [ebx + 0x38], 0 / call SEARCH_CLEANUP / jmp NO_FALLBACK_RESUME
    const uintptr_t noFallback = cave.here();
    cave.emit8(0xA1);
    cave.emit32(address(&allyFallback.cell));
    const BYTE testEax[2] = {0x85, 0xC0};
    cave.emit(testEax);
    const size_t stockJump = cave.size;
    cave.emit(shortJe);
    const BYTE storeFallback[3] = {0x89, 0x45, 0x50};
    cave.emit(storeFallback);
    cave.jmp(FALLBACK_PATH);
    cave.resolveShortJump(stockJump, cave.size);
    const BYTE clearTunneling[4] = {0xC6, 0x43, 0x38, 0x00};
    cave.emit(clearTunneling);
    cave.call(SEARCH_CLEANUP);
    cave.jmp(NO_FALLBACK_RESUME);

    static PatchSet set;
    set.reset();

    // mov [ebp + 0x50], edi / mov [ebp + 0x24], edi / mov [ebp + 0x34], eax: no fallback, index 0 and distance 0x7FFFFFFF at the
    // search's start; the loop writes the three slots nowhere else
    const BYTE slotsStart[9] = {0x89, 0x7D, 0x50, 0x89, 0x7D, 0x24, 0x89, 0x45, 0x34};
    set.expect(0x006FD992, slotsStart);
    // push [esi] / mov ecx, [ebp + 0x78] / call PATH_INDEX / cmp eax, [ebp + 0x24] / jg / jne / cmp edi, [ebp + 0x34] / jge: the
    // hierarchical branch's comparison with the record, with the call the cave repeats
    const BYTE hierarchicalCompare[30] = {0xFF, 0x36, 0x8B, 0x4D, 0x78, 0xE8, 0x13, 0xD3, 0x23, 0x00, 0x3B, 0x45, 0x24, 0x7F, 0x0F,
                                          0x0F, 0x85, 0xF4, 0x00, 0x00, 0x00, 0x3B, 0x7D, 0x34, 0x0F, 0x8D, 0xEB, 0x00, 0x00, 0x00};
    set.expectBytes(0x006FDA76, hierarchicalCompare);
    // cmp edi, [ebp + 0x34] / jge: the flat branch's comparison
    const BYTE flatCompare[5] = {0x3B, 0x7D, 0x34, 0x7D, 0x74};
    set.expect(0x006FDB06, flatCompare);

    // mov [ebp + 0x24], eax
    const BYTE indexStore[3] = {0x89, 0x45, 0x24};
    const BYTE nops[3] = {0x90, 0x90, 0x90};
    set.replace(INDEX_STORE, indexStore, nops);
    // mov [ebp + 0x34], edi
    const BYTE distanceStore[3] = {0x89, 0x7D, 0x34};
    set.replace(HIERARCHICAL_DISTANCE_STORE, distanceStore, nops);
    set.replace(FLAT_DISTANCE_STORE, distanceStore, nops);

    // test al, al / je 0x006FDB7F / mov [ebp + 0x50], esi
    const BYTE store[7] = {0x84, 0xC0, 0x74, 0x03, 0x89, 0x75, 0x50};
    set.hook(FALLBACK_STORE, store, fallbackStore);
    // cmp byte [ebp + 0x5F], 0: the resume point reads no register the cave changes
    const BYTE resumeCode[4] = {0x80, 0x7D, 0x5F, 0x00};
    set.expect(FALLBACK_STORE_RESUME, resumeCode);
    // push ebp / mov ebp, esp / push ecx / push ecx / push esi / push [ebp + 8], and its ret 4
    const BYTE pathIndexStart[9] = {0x55, 0x8B, 0xEC, 0x51, 0x51, 0x56, 0xFF, 0x75, 0x08};
    set.expect(PATH_INDEX, pathIndexStart);
    const BYTE pathIndexReturn[3] = {0xC2, 0x04, 0x00};
    set.expect(0x0093ADE3, pathIndexReturn);

    if (allies) {
        set.hook(SEARCH_START, slotStores, searchStart);
        // mov [ebp + 0x20], eax: what follows the replaced stores
        const BYTE afterSlots[3] = {0x89, 0x45, 0x20};
        set.expect(SEARCH_START_RESUME, afterSlots);
        // mov byte [ebx + 0x38], 0 / call SEARCH_CLEANUP: the search's end without a fallback
        const BYTE noFallbackCode[9] = {0xC6, 0x43, 0x38, 0x00, 0xE8, 0x67, 0x77, 0xFF, 0xFF};
        set.hook(NO_FALLBACK, noFallbackCode, noFallback);
        // mov ecx, [ebp + 0x58]: what follows it
        const BYTE afterNoFallback[3] = {0x8B, 0x4D, 0x58};
        set.expect(NO_FALLBACK_RESUME, afterNoFallback);
        // cmp dword [ebp + 0x50], 0 / mov ecx, ebx / je NO_FALLBACK, then the fallback's path: mov eax, [ebp + 0x7C] / push 1 / push 0 /
        // push [ebp + 0x54] / mov byte [eax], 1 (the retry flag) / push [ebp + 0x50]
        const BYTE fallbackTest[8] = {0x83, 0x7D, 0x50, 0x00, 0x8B, 0xCB, 0x74, 0x3C};
        set.expect(0x006FDD65, fallbackTest);
        const BYTE fallbackPath[16] = {0x8B, 0x45, 0x7C, 0x6A, 0x01, 0x6A, 0x00, 0xFF, 0x75, 0x54, 0xC6, 0x00, 0x01, 0xFF, 0x75, 0x50};
        set.expect(FALLBACK_PATH, fallbackPath);
        // the checks' arguments as the search passes them: push [ebp + 0x54] / mov edx, [esi + 0xC] / push [ebp + 0x2C] / mov eax, [esi] /
        // mov ecx, [eax + 4] / mov eax, [eax] / shr edx, 4 / and edx, 0x3F / push edx / push ecx / push eax / push [ebp + 0x68]
        const BYTE cellCheckArguments[24] = {0xFF, 0x75, 0x54, 0x8B, 0x56, 0x0C, 0xFF, 0x75, 0x2C, 0x8B, 0x06, 0x8B, 0x48, 0x04, 0x8B, 0x00,
                                             0xC1, 0xEA, 0x04, 0x83, 0xE2, 0x3F, 0x52, 0x51};
        set.expect(0x006FDB0B, cellCheckArguments);
        // push esi / lea eax, [ebp - 0xC] / push eax / mov ecx, ebx / call ZONE_CHECK
        const BYTE zoneCheckArguments[11] = {0x56, 0x8D, 0x45, 0xF4, 0x50, 0x8B, 0xCB, 0xE8, 0xBF, 0xA6, 0xFE};
        set.expect(0x006FDB35, zoneCheckArguments);
        // push esi / push [ebp + 0x54] / lea eax, [ebp + 8] / push eax / call CELL_CENTRE / add esp, 0xC
        const BYTE centreArguments[16] = {0x56, 0xFF, 0x75, 0x54, 0x8D, 0x45, 0x08, 0x50, 0xE8, 0x4B, 0xC5, 0xFE, 0xFF, 0x83, 0xC4, 0x0C};
        set.expect(0x006FDB49, centreArguments);
        // the footprint scan the ally-blind copy follows (0x006EF3C3): its helpers' calls, the container, the cell lookup and flags,
        // and the unit and goal lists' tests
        const BYTE scanSetup[40] = {0x8D, 0x45, 0xF8, 0x50, 0x8D, 0x45, 0x08, 0x50, 0x53, 0x89, 0x4D, 0xEC, 0xE8, 0xC0, 0xDC, 0xFF,
                                    0xFF, 0x8B, 0x7D, 0x0C, 0x57, 0x8D, 0x45, 0xE0, 0x53, 0x50, 0xE8, 0xF0, 0xDB, 0xFF, 0xFF, 0x8B,
                                    0x0D, 0x90, 0x46, 0xDE, 0x00, 0x83, 0xC4, 0x18};
        set.expectBytes(0x006EF3CF, scanSetup);
        const BYTE scanLayer[7] = {0x57, 0x53, 0xE8, 0x77, 0x16, 0xF9, 0xFF};
        set.expect(0x006EF3F7, scanLayer);
        const BYTE scanContainer[6] = {0x8B, 0x83, 0x7C, 0x02, 0x00, 0x00};
        set.expect(0x006EF419, scanContainer);
        const BYTE scanCell[15] = {0xFF, 0x75, 0x08, 0x8B, 0x4D, 0xEC, 0x57, 0xFF, 0x75, 0xF0, 0xE8, 0x2B, 0x3A, 0xEF, 0xFF};
        set.expect(0x006EF462, scanCell);
        const BYTE scanFlags[11] = {0x8B, 0x46, 0x0C, 0x8B, 0xC8, 0x83, 0xE1, 0x0F, 0x80, 0xF9, 0x05};
        set.expect(0x006EF47B, scanFlags);
        const BYTE scanFlagged[14] = {0xC1, 0xE8, 0x12, 0xA8, 0x01, 0x74, 0x0F, 0x8B, 0xCB, 0xE8, 0xF0, 0xC1, 0xF9, 0xFF};
        set.expect(0x006EF48C, scanFlagged);
        const BYTE scanUnitList[9] = {0x8B, 0x36, 0x85, 0xF6, 0x74, 0x05, 0x8B, 0x4E, 0x20};
        set.expect(0x006EF4C3, scanUnitList);
        const BYTE scanUnitTests[63] = {0x8B, 0x41, 0x08, 0x3B, 0xC3, 0x74, 0x3A, 0x8B, 0x55, 0xFC, 0x39, 0x50, 0x74, 0x74, 0x32, 0x8B,
                                        0x53, 0x04, 0x8B, 0x92, 0x14, 0x01, 0x00, 0x00, 0xF6, 0xC6, 0x20, 0x75, 0x0C, 0x8B, 0x78, 0x04,
                                        0xF6, 0x87, 0x15, 0x01, 0x00, 0x00, 0x20, 0x75, 0x18, 0xF7, 0xC2, 0x00, 0x00, 0x00, 0x20, 0x0F,
                                        0x84, 0x84, 0x00, 0x00, 0x00, 0x8B, 0x40, 0x04, 0xF6, 0x80, 0x09, 0x01, 0x00, 0x00, 0x01};
        set.expectBytes(0x006EF4D4, scanUnitTests);
        const BYTE scanGoalList[3] = {0x8B, 0x46, 0x14};
        set.expect(0x006EF522, scanGoalList);
        const BYTE scanGoalTests[53] = {0x8B, 0x48, 0x08, 0x3B, 0xCB, 0x74, 0x30, 0x8B, 0x49, 0x04, 0xBA, 0x00, 0x20, 0x00, 0x00, 0x85,
                                        0x91, 0x14, 0x01, 0x00, 0x00, 0x74, 0x0B, 0x8B, 0x73, 0x04, 0x85, 0x96, 0x14, 0x01, 0x00, 0x00,
                                        0x74, 0x15, 0xF6, 0x81, 0x09, 0x01, 0x00, 0x00, 0x01, 0x74, 0x37, 0x8B, 0x4B, 0x04, 0xF6, 0x81,
                                        0x17, 0x01, 0x00, 0x00, 0x20};
        set.expectBytes(0x006EF52B, scanGoalTests);
        // the relationship's ret 4
        const BYTE relationshipReturn[3] = {0xC2, 0x04, 0x00};
        set.expect(0x0068D8A5, relationshipReturn);
    }

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
