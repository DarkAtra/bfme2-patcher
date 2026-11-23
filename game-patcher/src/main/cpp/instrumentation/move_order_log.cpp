#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <unordered_map>

#include "instrumentation/move_order_log.h"
#include "patch_set.h"
#include "patcher_folder.h"

// Move order log (debug mode, with -movelog on the game's command line)
//
// Follows the units of group and formation move orders through the steps that can drop a unit's order in a crowd and writes one
// line per step to %TEMP%\bfme2-move-orders.csv:
//   group     the order picks the unit's destination: AdjustDestination (0x006FE456) with the group's destination, called from
//             0x0094E043 (group order) and 0x0094A64B, 0x0094B60A, 0x0094BBB0 (formation orders). The unit is followed from then on
//             for TRACK_FRAMES logic frames; the other events are logged for followed units only.
//   adjust    the move state's onEnter (0x0074DADA) adjusts the goal again (AdjustDestination without a group destination,
//             0x0074DC65); when that fails, it snaps the goal to its cell.
//   findpath  FindPath's search (InternalFindPath 0x006FD06F, its only call 0x006FEAB2). Retry going from 0 to 1 means a path to the
//             closest cell the search reached, not to the goal. The detail tells how the search ended: goal, early (returned before
//             searching), limit (more cells than MaxCellsFindPathLimit, GlobalData + 0x1210) or exhausted (no open cell left), both
//             leaving at 0x006FDD65; the cells it examined; flag (pathfinder + 0x38: no closest cell is kept) and blocked (the goal
//             failed its checks); whether it kept a closest cell; how many closest-so-far cells it tested and how many passed each of
//             the three checks a closest cell needs (0x006F3082, 0x006E8200, 0x006EF3C3, called from 0x006FDAB8 / 0x006FDB2C,
//             0x006FDAC8 / 0x006FDB3C, 0x006FDAEB / 0x006FDB5F); how many the first tested cell, the unit's own, passed; the
//             search's time in microseconds; what blocked the footprint scan where it failed ("footprint": failures, then the
//             blocking objects in the cells' unit lists and goal lists, each as the unit's own soldiers (contained by it, +0x27C) and
//             others, blocked cells, and the first other object's template); and for a search that examined no cell, the same for
//             the footprints of the eight cells around the unit ("boxed"); the other objects are also split by their relationship to
//             the unit (0x0068D7AB); for a search that gave up, the nearest it came to the goal, in cells ("nearest").
// Test aid, the only write: -pathlimit <n> on the game's command line sets MaxCellsFindPathLimit (GlobalData + 0x1210) before every
// search, to see what a higher limit finds and costs.
//   path      AIUpdateInterface::computePath (0x00665C33) from doPathfind (0x00669519) and from 0x006668CA (0x00666A5F). Without a
//             path the move state fails once the request is done (0x00748EE4) and the unit goes idle.
// Columns: logic frame, event, call site, object id, template, the unit's position, the destination going in, the destination or
// path end coming out, the function's result and a detail. The hooks redirect the calls to wrappers that call the game's function
// and only read what it returned.

using namespace patching;

namespace {

    constexpr uintptr_t ADJUST_DESTINATION = 0x006FE456;
    constexpr uintptr_t INTERNAL_FIND_PATH = 0x006FD06F;
    constexpr uintptr_t COMPUTE_PATH = 0x00665C33;

    constexpr uintptr_t GROUP_ORDER_ADJUST = 0x0094E043;
    constexpr uintptr_t FORMATION_ADJUST_1 = 0x0094A64B;
    constexpr uintptr_t FORMATION_ADJUST_2 = 0x0094B60A;
    constexpr uintptr_t FORMATION_ADJUST_3 = 0x0094BBB0;
    constexpr uintptr_t MOVE_STATE_ADJUST = 0x0074DC65;
    constexpr uintptr_t FIND_PATH_SEARCH = 0x006FEAB2;
    constexpr uintptr_t DO_PATHFIND_COMPUTE = 0x00669519;
    constexpr uintptr_t OTHER_COMPUTE = 0x00666A5F;

    constexpr uintptr_t CELL_CHECK = 0x006F3082;
    constexpr uintptr_t ZONE_CHECK = 0x006E8200;
    constexpr uintptr_t POSITION_CHECK = 0x006EF3C3;
    constexpr uintptr_t CELL_CHECKS[2] = {0x006FDAB8, 0x006FDB2C};
    constexpr uintptr_t ZONE_CHECKS[2] = {0x006FDAC8, 0x006FDB3C};
    constexpr uintptr_t POSITION_CHECKS[2] = {0x006FDAEB, 0x006FDB5F};
    constexpr uintptr_t SEARCH_GIVES_UP = 0x006FDD65;
    constexpr uintptr_t SEARCH_GIVES_UP_RESUME = 0x006FDD6B;

    constexpr uintptr_t GLOBAL_DATA = 0x00DE4364;
    constexpr size_t FIND_PATH_LIMIT = 0x1210;
    // the search's frame (ebp, 0x60 below the frame base) and the pathfinder (ebx) where it gives up
    constexpr size_t SEARCH_CELLS = 0x38;
    constexpr size_t SEARCH_CANDIDATE = 0x50;
    constexpr size_t SEARCH_GOAL_BLOCKED = 0x5F;
    constexpr size_t SEARCH_NEAREST = 0x20;
    constexpr size_t PATHFINDER_NO_CANDIDATES = 0x38;

    // the footprint scan's helpers: cdecl(object, int *before, int *after) the footprint around a cell, cdecl(cell index *out,
    // object, position) the cell of a position, thiscall(terrain logic, object, position) its layer, thiscall(pathfinder, layer, x, y)
    // a cell, thiscall(object) the test for the cells flagged at bit 18
    constexpr uintptr_t FOOTPRINT = 0x006ED0A0;
    constexpr uintptr_t CELL_OF = 0x006ECFDE;
    constexpr uintptr_t LAYER_OF = 0x00680A75;
    constexpr uintptr_t TERRAIN_LOGIC = 0x00DE4690;
    constexpr uintptr_t GET_CELL = 0x005E2E9C;
    constexpr uintptr_t FLAGGED_CELL_TEST = 0x0068B68A;
    constexpr size_t OBJECT_CONTAINED_BY = 0x27C;
    constexpr size_t TEMPLATE_KINDS = 0x114;
    constexpr uint32_t KIND_HORDE = 0x2000;
    constexpr uint32_t KIND_CRUSHER = 0x20000000;
    constexpr size_t TEMPLATE_CRUSHABLE = 0x109;
    constexpr size_t TEMPLATE_GOAL_PASS = 0x117;
    constexpr size_t CELL_FLAGS = 0x0C;
    constexpr size_t INFO_UNITS = 0x20;
    constexpr size_t INFO_GOALS = 0x14;
    // thiscall(object, other): 0 enemies, 1 neutral, 2 allies
    constexpr uintptr_t RELATIONSHIP = 0x0068D7AB;
    constexpr uint32_t ALLIES = 2;
    constexpr uint32_t ENEMIES = 0;

    constexpr uintptr_t GAME_LOGIC = 0x00DE412C;
    constexpr size_t LOGIC_FRAME = 0x40;
    constexpr size_t OBJECT_TEMPLATE = 0x04;
    constexpr size_t OBJECT_POSITION = 0x38;
    constexpr size_t OBJECT_ID = 0x74;
    constexpr size_t TEMPLATE_NAME = 0x64;
    constexpr size_t NAME_TEXT = 0x08;
    constexpr size_t AI_OBJECT = 0x08;
    constexpr size_t AI_PATH = 0x140;
    constexpr size_t PATH_LAST_NODE = 0x08;
    constexpr size_t NODE_POSITION = 0x0C;

    constexpr uint32_t TRACK_FRAMES = 300;

    std::unordered_map<uint32_t, uint32_t> *followed = nullptr;
    LARGE_INTEGER frequency = {};
    uint32_t pathLimit = 0;

    struct Blockers {
        uint32_t own;
        uint32_t others;
        uint32_t allies;
        uint32_t enemies;
        uint32_t goalOwn;
        uint32_t goalOthers;
        uint32_t cells;
        const char *sample;
    };

    struct Search {
        bool gaveUp;
        uint32_t cells;
        uint8_t noCandidates;
        uint8_t goalBlocked;
        bool candidate;
        uint32_t tested;
        uint32_t passed[3];
        int32_t first;
        uint32_t nearest;
        uint32_t footprintFails;
        Blockers footprint;
        Blockers boxed;
    } search;
    FILE *file = nullptr;
    bool failed = false;

    // The number after `name` on the game's command line, 0 when it is missing.
    uint32_t argumentValue(const wchar_t *name) {
        const wchar_t *line = GetCommandLineW();
        const size_t length = wcslen(name);
        for (const wchar_t *position = line; *position != L'\0'; position++) {
            const bool start = position == line || position[-1] == L' ' || position[-1] == L'\t';
            const wchar_t end = position[length];
            if (start && _wcsnicmp(position, name, length) == 0 && (end == L' ' || end == L'\t')) {
                return wcstoul(position + length, nullptr, 10);
            }
        }
        return 0;
    }

    template<typename T>
    T read(const void *base, const size_t offset) {
        return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
    }

    uint32_t logicFrame() {
        const auto *logic = *reinterpret_cast<const uint8_t *const *>(GAME_LOGIC);
        return logic != nullptr ? read<uint32_t>(logic, LOGIC_FRAME) : 0;
    }

    uint32_t objectId(const uint8_t *object) {
        return object != nullptr ? read<uint32_t>(object, OBJECT_ID) : 0;
    }

    const char *templateName(const uint8_t *object) {
        const auto *thing = object != nullptr ? read<const uint8_t *>(object, OBJECT_TEMPLATE) : nullptr;
        const auto *name = thing != nullptr ? read<const uint8_t *>(thing, TEMPLATE_NAME) : nullptr;
        return name != nullptr ? reinterpret_cast<const char *>(name + NAME_TEXT) : "";
    }

    const float *pathEnd(const uint8_t *path) {
        const auto *node = path != nullptr ? read<const uint8_t *>(path, PATH_LAST_NODE) : nullptr;
        return node != nullptr ? reinterpret_cast<const float *>(node + NODE_POSITION) : nullptr;
    }

    bool isFollowed(const uint32_t id, const uint32_t frame) {
        if (followed == nullptr) {
            return false;
        }
        const auto entry = followed->find(id);
        return entry != followed->end() && frame - entry->second <= TRACK_FRAMES;
    }

    bool isGroupSite(const uintptr_t site) {
        return site == GROUP_ORDER_ADJUST || site == FORMATION_ADJUST_1 || site == FORMATION_ADJUST_2 || site == FORMATION_ADJUST_3;
    }

    void writeLine(const uint32_t frame, const char *event, const uintptr_t site, const uint8_t *object, const float *in,
                   const float *out, const uint32_t result, const char *detail) {
        if (file == nullptr) {
            if (failed) {
                return;
            }
            wchar_t path[MAX_PATH];
            if (!tempFilePath(path, L"bfme2-move-orders.csv") || _wfopen_s(&file, path, L"w") != 0 || file == nullptr) {
                failed = true;
                return;
            }
            fprintf(file, "frame,event,site,object,template,pos_x,pos_y,in_x,in_y,out_x,out_y,result,detail\n");
        }
        const auto *position = object != nullptr ? reinterpret_cast<const float *>(object + OBJECT_POSITION) : nullptr;
        fprintf(file, "%u,%s,%08X,%u,%s,", frame, event, static_cast<unsigned>(site), objectId(object), templateName(object));
        const float *points[3] = {position, in, out};
        for (const float *point : points) {
            if (point != nullptr) {
                fprintf(file, "%.1f,%.1f,", point[0], point[1]);
            } else {
                fprintf(file, ",,");
            }
        }
        fprintf(file, "%u,%s\n", result, detail);
        fflush(file);
    }

    using AdjustDestination = uint32_t (__thiscall *)(void *, uint8_t *, void *, float *, const float *);
    using InternalFindPath = uint8_t *(__thiscall *)(void *, uint8_t *, void *, const float *, const float *, void *, uint8_t *);
    using ComputePath = uint32_t (__thiscall *)(void *, void *, const float *);
    using CellCheck = uint32_t (__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using PairCheck = uint32_t (__thiscall *)(void *, uint32_t, uint32_t);

    void countCheck(const size_t check, const uint32_t result) {
        if (check == 0) {
            search.tested++;
            if (search.tested == 1) {
                search.first = 0;
            }
        }
        if ((result & 0xFF) == 0) {
            return;
        }
        search.passed[check]++;
        if (search.tested == 1) {
            search.first++;
        }
    }

    __declspec(noinline) uint32_t __fastcall cellCheck(void *pathfinder, void *, const uint32_t a, const uint32_t b, const uint32_t c,
                                                       const uint32_t d, const uint32_t e, const uint32_t f) {
        const uint32_t result = reinterpret_cast<CellCheck>(CELL_CHECK)(pathfinder, a, b, c, d, e, f);
        countCheck(0, result);
        return result;
    }

    __declspec(noinline) uint32_t __fastcall zoneCheck(void *pathfinder, void *, const uint32_t a, const uint32_t b) {
        const uint32_t result = reinterpret_cast<PairCheck>(ZONE_CHECK)(pathfinder, a, b);
        countCheck(1, result);
        return result;
    }

    struct CellIndex {
        int32_t x;
        int32_t y;
    };

    uint32_t kinds(const uint8_t *object) {
        return read<uint32_t>(read<const uint8_t *>(object, OBJECT_TEMPLATE), TEMPLATE_KINDS);
    }

    uint8_t templateByte(const uint8_t *object, const size_t offset) {
        return read<uint8_t>(read<const uint8_t *>(object, OBJECT_TEMPLATE), offset);
    }

    void count(Blockers &blockers, const uint8_t *mover, const uint8_t *other, const bool goal) {
        const bool own = read<const uint8_t *>(other, OBJECT_CONTAINED_BY) == mover;
        if (goal) {
            (own ? blockers.goalOwn : blockers.goalOthers)++;
        } else {
            (own ? blockers.own : blockers.others)++;
        }
        if (own) {
            return;
        }
        const uint32_t relationship = reinterpret_cast<uint32_t (__thiscall *)(const uint8_t *, const uint8_t *)>(RELATIONSHIP)(mover, other);
        if (relationship == ALLIES) {
            blockers.allies++;
        } else if (relationship == ENEMIES) {
            blockers.enemies++;
        }
        if (blockers.sample == nullptr) {
            blockers.sample = templateName(other);
        }
    }

    // The footprint scan's tests (0x006EF3C3) for one cell's footprint, counting every blocker instead of stopping at the first.
    void classifyFootprint(void *pathfinder, uint8_t *mover, const CellIndex cell, const uint32_t layer, Blockers &blockers) {
        int32_t before = 0;
        int32_t after = 0;
        reinterpret_cast<void (__cdecl *)(uint8_t *, int32_t *, int32_t *)>(FOOTPRINT)(mover, &before, &after);
        const auto *container = read<const uint8_t *>(mover, OBJECT_CONTAINED_BY);
        const uint32_t excluded = container != nullptr ? read<uint32_t>(container, OBJECT_ID) : 0;
        const uint32_t moverKinds = kinds(mover);
        const auto getCell = reinterpret_cast<const uint8_t *(__thiscall *)(void *, uint32_t, int32_t, int32_t)>(GET_CELL);
        const auto flaggedCellTest = reinterpret_cast<uint8_t (__thiscall *)(uint8_t *)>(FLAGGED_CELL_TEST);
        for (int32_t x = cell.x - before; x < cell.x + after; x++) {
            for (int32_t y = cell.y - before; y < cell.y + after; y++) {
                const uint8_t *pathCell = getCell(pathfinder, layer, x, y);
                if (pathCell == nullptr) {
                    blockers.cells++;
                    continue;
                }
                const uint32_t flags = read<uint32_t>(pathCell, CELL_FLAGS);
                const uint32_t type = flags & 0xF;
                if (type == 4 || type == 5 || type == 6 || (((flags >> 18) & 1) != 0 && flaggedCellTest(mover) != 0)) {
                    blockers.cells++;
                    continue;
                }
                const auto *info = read<const uint8_t *>(pathCell, 0);
                if (info == nullptr) {
                    continue;
                }
                for (auto *node = read<const uint8_t *>(info, INFO_UNITS); node != nullptr; node = read<const uint8_t *>(node, 0)) {
                    const auto *other = read<const uint8_t *>(node, 8);
                    if (other == mover || read<uint32_t>(other, OBJECT_ID) == excluded) {
                        continue;
                    }
                    if ((moverKinds & KIND_HORDE) == 0 && (kinds(other) & KIND_HORDE) != 0) {
                        continue;
                    }
                    if ((moverKinds & KIND_CRUSHER) != 0 && (templateByte(other, TEMPLATE_CRUSHABLE) & 1) != 0) {
                        continue;
                    }
                    count(blockers, mover, other, false);
                }
                for (auto *node = read<const uint8_t *>(info, INFO_GOALS); node != nullptr; node = read<const uint8_t *>(node, 0)) {
                    const auto *other = read<const uint8_t *>(node, 8);
                    if (other == mover || ((kinds(other) & KIND_HORDE) != 0 && (moverKinds & KIND_HORDE) == 0)) {
                        continue;
                    }
                    if ((templateByte(other, TEMPLATE_CRUSHABLE) & 1) != 0 && (templateByte(mover, TEMPLATE_GOAL_PASS) & 0x20) != 0) {
                        continue;
                    }
                    count(blockers, mover, other, true);
                }
            }
        }
    }

    CellIndex cellOf(uint8_t *mover, const float *position) {
        CellIndex cell = {};
        reinterpret_cast<void (__cdecl *)(CellIndex *, uint8_t *, const float *)>(CELL_OF)(&cell, mover, position);
        return cell;
    }

    uint32_t layerOf(uint8_t *mover, const float *position) {
        void *terrain = *reinterpret_cast<void **>(TERRAIN_LOGIC);
        return reinterpret_cast<uint32_t (__thiscall *)(void *, uint8_t *, const float *)>(LAYER_OF)(terrain, mover, position);
    }

    __declspec(noinline) uint32_t __fastcall positionCheck(void *pathfinder, void *, const uint32_t a, const uint32_t b) {
        auto *mover = reinterpret_cast<uint8_t *>(a);
        const auto *position = reinterpret_cast<const float *>(b);
        const float before[3] = {position[0], position[1], position[2]};
        const uint32_t result = reinterpret_cast<PairCheck>(POSITION_CHECK)(pathfinder, a, b);
        countCheck(2, result);
        if ((result & 0xFF) == 0 && isFollowed(objectId(mover), logicFrame())) {
            search.footprintFails++;
            classifyFootprint(pathfinder, mover, cellOf(mover, before), layerOf(mover, before), search.footprint);
        }
        return result;
    }

    void classifyBoxed(void *pathfinder, uint8_t *mover) {
        const auto *position = reinterpret_cast<const float *>(mover + OBJECT_POSITION);
        const CellIndex start = cellOf(mover, position);
        const uint32_t layer = layerOf(mover, position);
        for (int32_t dx = -1; dx <= 1; dx++) {
            for (int32_t dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    classifyFootprint(pathfinder, mover, {start.x + dx, start.y + dy}, layer, search.boxed);
                }
            }
        }
    }

    void appendBlockers(char *detail, const size_t size, const char *name, const Blockers &blockers) {
        const size_t used = strlen(detail);
        sprintf_s(detail + used, size - used, " %s own %u others %u (allies %u enemies %u) goal-own %u goal-others %u blocked-cells %u sample %s",
                  name, blockers.own, blockers.others, blockers.allies, blockers.enemies, blockers.goalOwn, blockers.goalOthers, blockers.cells,
                  blockers.sample != nullptr ? blockers.sample : "-");
    }

    void __stdcall searchGivesUp(const uint8_t *frame, const uint8_t *pathfinder) {
        search.gaveUp = true;
        search.cells = read<uint32_t>(frame, SEARCH_CELLS);
        search.candidate = read<uint32_t>(frame, SEARCH_CANDIDATE) != 0;
        search.goalBlocked = frame[SEARCH_GOAL_BLOCKED];
        search.nearest = read<uint32_t>(frame, SEARCH_NEAREST);
        search.noCandidates = pathfinder[PATHFINDER_NO_CANDIDATES];
    }

    const char *searchEnd(const uint8_t *path, const uint32_t limit) {
        if (!search.gaveUp) {
            return path != nullptr ? "goal" : "early";
        }
        return search.cells > limit ? "limit" : "exhausted";
    }

    __declspec(noinline) uint32_t __fastcall adjustDestination(void *pathfinder, void *, uint8_t *object, void *locomotors,
                                                               float *destination, const float *group) {
        const uintptr_t site = reinterpret_cast<uintptr_t>(_ReturnAddress()) - 5;
        const float in[2] = {destination[0], destination[1]};
        const uint32_t result = reinterpret_cast<AdjustDestination>(ADJUST_DESTINATION)(pathfinder, object, locomotors, destination,
                                                                                         group);
        const uint32_t frame = logicFrame();
        const uint32_t id = objectId(object);
        const bool groupSite = isGroupSite(site);
        if (groupSite && followed != nullptr) {
            (*followed)[id] = frame;
        } else if (!isFollowed(id, frame)) {
            return result;
        }
        char detail[64] = "";
        if (group != nullptr) {
            sprintf_s(detail, "group %.1f %.1f", group[0], group[1]);
        }
        writeLine(frame, groupSite ? "group" : "adjust", site, object, in, destination, result & 0xFF, detail);
        return result;
    }

    __declspec(noinline) uint8_t *__fastcall internalFindPath(void *pathfinder, void *, uint8_t *object, void *locomotors,
                                                              const float *from, const float *to, void *hierarchy, uint8_t *retry) {
        const uintptr_t site = reinterpret_cast<uintptr_t>(_ReturnAddress()) - 5;
        const uint8_t retryBefore = *retry;
        search = {};
        search.first = -1;
        if (pathLimit != 0) {
            *reinterpret_cast<uint32_t *>(*reinterpret_cast<uint8_t **>(GLOBAL_DATA) + FIND_PATH_LIMIT) = pathLimit;
        }
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        uint8_t *path = reinterpret_cast<InternalFindPath>(INTERNAL_FIND_PATH)(pathfinder, object, locomotors, from, to, hierarchy, retry);
        QueryPerformanceCounter(&end);
        const uint32_t frame = logicFrame();
        if (isFollowed(objectId(object), frame)) {
            if (search.gaveUp && search.cells == 0) {
                classifyBoxed(pathfinder, object);
            }
            const uint32_t limit = read<uint32_t>(*reinterpret_cast<const uint8_t *const *>(GLOBAL_DATA), FIND_PATH_LIMIT);
            char detail[512];
            const double microseconds = static_cast<double>(end.QuadPart - start.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart);
            sprintf_s(detail, "retry %u>%u hierarchy %u end %s cells %u/%u flag %u blocked %u candidate %u tested %u passed %u/%u/%u first %d us %.0f",
                      retryBefore, *retry, hierarchy != nullptr ? 1u : 0u, searchEnd(path, limit), search.cells, limit, search.noCandidates,
                      search.goalBlocked, search.candidate ? 1u : 0u, search.tested, search.passed[0], search.passed[1], search.passed[2],
                      search.first, microseconds);
            if (search.footprintFails > 0) {
                const size_t used = strlen(detail);
                sprintf_s(detail + used, sizeof(detail) - used, " footprint %u", search.footprintFails);
                appendBlockers(detail, sizeof(detail), "blockers", search.footprint);
            }
            if (search.gaveUp && search.cells == 0) {
                appendBlockers(detail, sizeof(detail), "boxed", search.boxed);
            }
            if (search.gaveUp) {
                const size_t used = strlen(detail);
                sprintf_s(detail + used, sizeof(detail) - used, " nearest %.1f", sqrt(static_cast<double>(search.nearest)));
            }
            writeLine(frame, "findpath", site, object, to, pathEnd(path), path != nullptr ? 1 : 0, detail);
        }
        return path;
    }

    __declspec(noinline) uint32_t __fastcall computePath(void *ai, void *, void *services, const float *destination) {
        const uintptr_t site = reinterpret_cast<uintptr_t>(_ReturnAddress()) - 5;
        const float in[2] = {destination[0], destination[1]};
        const uint32_t result = reinterpret_cast<ComputePath>(COMPUTE_PATH)(ai, services, destination);
        const auto *object = read<const uint8_t *>(ai, AI_OBJECT);
        const uint32_t frame = logicFrame();
        if (isFollowed(objectId(object), frame)) {
            const auto *path = read<const uint8_t *>(ai, AI_PATH);
            writeLine(frame, "path", site, object, in, pathEnd(path), result & 0xFF, path != nullptr ? "path" : "no path");
        }
        return result;
    }

    void redirect(PatchSet &set, const uintptr_t site, const uintptr_t callee, const void *wrapper) {
        BYTE stock[5] = {0xE8};
        const auto displacement = static_cast<int32_t>(callee - (site + 5));
        memcpy(stock + 1, &displacement, sizeof(displacement));
        set.redirectCall(site, stock, reinterpret_cast<uintptr_t>(wrapper));
    }
}

bool instrumentation::applyMoveOrderLog() {
    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();

    redirect(set, GROUP_ORDER_ADJUST, ADJUST_DESTINATION, reinterpret_cast<const void *>(&adjustDestination));
    redirect(set, FORMATION_ADJUST_1, ADJUST_DESTINATION, reinterpret_cast<const void *>(&adjustDestination));
    redirect(set, FORMATION_ADJUST_2, ADJUST_DESTINATION, reinterpret_cast<const void *>(&adjustDestination));
    redirect(set, FORMATION_ADJUST_3, ADJUST_DESTINATION, reinterpret_cast<const void *>(&adjustDestination));
    redirect(set, MOVE_STATE_ADJUST, ADJUST_DESTINATION, reinterpret_cast<const void *>(&adjustDestination));
    redirect(set, FIND_PATH_SEARCH, INTERNAL_FIND_PATH, reinterpret_cast<const void *>(&internalFindPath));
    redirect(set, DO_PATHFIND_COMPUTE, COMPUTE_PATH, reinterpret_cast<const void *>(&computePath));
    redirect(set, OTHER_COMPUTE, COMPUTE_PATH, reinterpret_cast<const void *>(&computePath));
    for (size_t i = 0; i < 2; i++) {
        redirect(set, CELL_CHECKS[i], CELL_CHECK, reinterpret_cast<const void *>(&cellCheck));
        redirect(set, ZONE_CHECKS[i], ZONE_CHECK, reinterpret_cast<const void *>(&zoneCheck));
        redirect(set, POSITION_CHECKS[i], POSITION_CHECK, reinterpret_cast<const void *>(&positionCheck));
    }

    const uintptr_t entry = cave.here();
    cave.emit8(0x60); // pushad
    cave.emit8(0x53); // push ebx: the pathfinder
    cave.emit8(0x55); // push ebp: the search's frame
    cave.call(reinterpret_cast<uintptr_t>(&searchGivesUp));
    cave.emit8(0x61); // popad
    // cmp dword [ebp + 0x50], 0 / mov ecx, ebx: the closest cell kept, then the je at the resume address
    const BYTE givesUp[6] = {0x83, 0x7D, 0x50, 0x00, 0x8B, 0xCB};
    cave.emit(givesUp);
    cave.jmp(SEARCH_GIVES_UP_RESUME);
    set.hook(SEARCH_GIVES_UP, givesUp, entry);
    // je 0x006FDDA9: no closest cell
    const BYTE resume[2] = {0x74, 0x3C};
    set.expect(SEARCH_GIVES_UP_RESUME, resume);
    // mov eax, [GlobalData] / mov ecx, [ebp + 0x38] / cmp ecx, [eax + 0x1210] / ja: the cells examined against the limit, and
    // cmp byte [ebx + 0x38], 0: the pathfinder's flag that skips the closest cell
    const BYTE limitTest[20] = {0xA1, 0x64, 0x43, 0xDE, 0x00, 0x8B, 0x4D, 0x38, 0x3B, 0x88, 0x10, 0x12, 0x00, 0x00, 0x0F, 0x87, 0x50, 0x03,
                                0x00, 0x00};
    set.expect(0x006FD9C3, limitTest);
    const BYTE noCandidatesTest[4] = {0x80, 0x7B, 0x38, 0x00};
    set.expect(0x006FD9D7, noCandidatesTest);
    // cmp byte [ebp + 0x5F], 0: the goal-blocked flag (the closest cell's store before it belongs to pathfinder_closest_cell.cpp)
    const BYTE goalBlockedTest[4] = {0x80, 0x7D, 0x5F, 0x00};
    set.expect(0x006FDB7F, goalBlockedTest);
    // cmp edi, [ebp + 0x20] / mov [ebp + 0x1C], edi: the nearest distance to the goal so far (squared, in cells), and its store
    const BYTE nearestTest[6] = {0x3B, 0x7D, 0x20, 0x89, 0x7D, 0x1C};
    set.expect(0x006FDA14, nearestTest);
    const BYTE nearestStore[3] = {0x89, 0x7D, 0x20};
    set.expect(0x006FDA50, nearestStore);
    // the relationship's ret 4 (thiscall(object, other))
    const BYTE relationshipReturn[3] = {0xC2, 0x04, 0x00};
    set.expect(0x0068D8A5, relationshipReturn);
    // the footprint scan the classifier repeats (0x006EF3C3)
    // push &after / push &before / push mover / call FOOTPRINT, then push position / push mover / push &cell / call CELL_OF, both
    // cdecl (add esp, 0x18), and the terrain logic for the layer
    const BYTE footprintSetup[40] = {0x8D, 0x45, 0xF8, 0x50, 0x8D, 0x45, 0x08, 0x50, 0x53, 0x89, 0x4D, 0xEC, 0xE8, 0xC0, 0xDC, 0xFF,
                                     0xFF, 0x8B, 0x7D, 0x0C, 0x57, 0x8D, 0x45, 0xE0, 0x53, 0x50, 0xE8, 0xF0, 0xDB, 0xFF, 0xFF, 0x8B,
                                     0x0D, 0x90, 0x46, 0xDE, 0x00, 0x83, 0xC4, 0x18};
    set.expectBytes(0x006EF3CF, footprintSetup);
    // push position / push mover / call LAYER_OF (thiscall on the terrain logic)
    const BYTE layerCall[7] = {0x57, 0x53, 0xE8, 0x77, 0x16, 0xF9, 0xFF};
    set.expect(0x006EF3F7, layerCall);
    // mov eax, [ebx + 0x27C]: the excluded object is the mover's container
    const BYTE containerRead[6] = {0x8B, 0x83, 0x7C, 0x02, 0x00, 0x00};
    set.expect(0x006EF419, containerRead);
    // push y / mov ecx, pathfinder / push x / push layer / call GET_CELL
    const BYTE cellLookup[15] = {0xFF, 0x75, 0x08, 0x8B, 0x4D, 0xEC, 0x57, 0xFF, 0x75, 0xF0, 0xE8, 0x2B, 0x3A, 0xEF, 0xFF};
    set.expect(0x006EF462, cellLookup);
    // mov eax, [esi + 0xC] / ... / cmp cl, 5: the cell's flags and type
    const BYTE cellFlags[11] = {0x8B, 0x46, 0x0C, 0x8B, 0xC8, 0x83, 0xE1, 0x0F, 0x80, 0xF9, 0x05};
    set.expect(0x006EF47B, cellFlags);
    // shr eax, 0x12 / test al, 1 / je / mov ecx, ebx / call FLAGGED_CELL_TEST
    const BYTE flaggedCell[14] = {0xC1, 0xE8, 0x12, 0xA8, 0x01, 0x74, 0x0F, 0x8B, 0xCB, 0xE8, 0xF0, 0xC1, 0xF9, 0xFF};
    set.expect(0x006EF48C, flaggedCell);
    // mov esi, [esi] / test esi, esi / je / mov ecx, [esi + 0x20]: the cell info's unit list
    const BYTE unitList[9] = {0x8B, 0x36, 0x85, 0xF6, 0x74, 0x05, 0x8B, 0x4E, 0x20};
    set.expect(0x006EF4C3, unitList);
    // the unit list's tests: itself, the excluded id, HORDE (template + 0x114 bit 0x2000), the crusher bit 0x20000000, crushable
    // (template + 0x109 bit 0)
    const BYTE unitTests[63] = {0x8B, 0x41, 0x08, 0x3B, 0xC3, 0x74, 0x3A, 0x8B, 0x55, 0xFC, 0x39, 0x50, 0x74, 0x74, 0x32, 0x8B, 0x53,
                                0x04, 0x8B, 0x92, 0x14, 0x01, 0x00, 0x00, 0xF6, 0xC6, 0x20, 0x75, 0x0C, 0x8B, 0x78, 0x04, 0xF6, 0x87,
                                0x15, 0x01, 0x00, 0x00, 0x20, 0x75, 0x18, 0xF7, 0xC2, 0x00, 0x00, 0x00, 0x20, 0x0F, 0x84, 0x84, 0x00,
                                0x00, 0x00, 0x8B, 0x40, 0x04, 0xF6, 0x80, 0x09, 0x01, 0x00, 0x00, 0x01};
    set.expectBytes(0x006EF4D4, unitTests);
    // mov eax, [esi + 0x14]: the goal list
    const BYTE goalList[3] = {0x8B, 0x46, 0x14};
    set.expect(0x006EF522, goalList);
    // the goal list's tests: itself, HORDE, crushable and the mover's template + 0x117 bit 5
    const BYTE goalTests[53] = {0x8B, 0x48, 0x08, 0x3B, 0xCB, 0x74, 0x30, 0x8B, 0x49, 0x04, 0xBA, 0x00, 0x20, 0x00, 0x00, 0x85, 0x91,
                                0x14, 0x01, 0x00, 0x00, 0x74, 0x0B, 0x8B, 0x73, 0x04, 0x85, 0x96, 0x14, 0x01, 0x00, 0x00, 0x74, 0x15,
                                0xF6, 0x81, 0x09, 0x01, 0x00, 0x00, 0x01, 0x74, 0x37, 0x8B, 0x4B, 0x04, 0xF6, 0x81, 0x17, 0x01, 0x00,
                                0x00, 0x20};
    set.expectBytes(0x006EF52B, goalTests);

    // the callees' stack cleanup the wrappers' __fastcall matches: ret 0x10 (AdjustDestination), ret 0x18 (InternalFindPath),
    // ret 8 (computePath), ret 0x18, ret 8 and ret 8 (the three checks)
    const BYTE cellCheckReturn[3] = {0xC2, 0x18, 0x00};
    set.expect(0x006F30AF, cellCheckReturn);
    const BYTE zoneCheckReturn[3] = {0xC2, 0x08, 0x00};
    set.expect(0x006E82A5, zoneCheckReturn);
    const BYTE positionCheckReturn[3] = {0xC2, 0x08, 0x00};
    set.expect(0x006EF58A, positionCheckReturn);
    const BYTE adjustReturn[3] = {0xC2, 0x10, 0x00};
    set.expect(0x006FE7C8, adjustReturn);
    const BYTE searchReturn[3] = {0xC2, 0x18, 0x00};
    set.expect(0x006FDDE2, searchReturn);
    const BYTE computeReturn[3] = {0xC2, 0x08, 0x00};
    set.expect(0x006668C7, computeReturn);
    // mov ecx, [GameLogic] / mov ecx, [ecx + 0x40]: the logic frame
    const BYTE frameRead[9] = {0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x49, 0x40};
    set.expect(0x00748EC9, frameRead);
    // in computePath (ebp = the AI): mov eax, [ebp + 8] / ... / mov ecx, [eax + 0x74] / mov eax, [eax + 4] / add eax, 0x64 /
    // mov eax, [eax] / test eax, eax / je / add eax, 8: the object, its id and its template's name
    const BYTE objectRead[3] = {0x8B, 0x45, 0x08};
    set.expect(0x00665C62, objectRead);
    const BYTE nameRead[18] = {0x8B, 0x48, 0x74, 0x8B, 0x40, 0x04, 0x83, 0xC0, 0x64, 0x8B, 0x00, 0x85, 0xC0, 0x74, 0x05, 0x83, 0xC0, 0x08};
    set.expect(0x00665C83, nameRead);
    // mov eax, [ebp + 8] / lea ecx, [esp + 0x50] / push ecx / push esi / lea ecx, [eax + 0x38]: the object's position
    const BYTE positionRead[12] = {0x8B, 0x45, 0x08, 0x8D, 0x4C, 0x24, 0x50, 0x51, 0x56, 0x8D, 0x48, 0x38};
    set.expect(0x00666462, positionRead);
    // in doPathfind (ebx = the AI): mov eax, [ebx + 0x140] / test eax, eax / je / mov eax, [eax + 8] / push [eax + 0x18] /
    // mov ecx, [ebx + 8] / add eax, 0xC: the path, its last node and the node's position
    const BYTE pathRead[22] = {0x8B, 0x83, 0x40, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x4E, 0x8B, 0x40, 0x08, 0xFF, 0x70, 0x18, 0x8B, 0x4B,
                               0x08, 0x83, 0xC0, 0x0C};
    set.expect(0x00669571, pathRead);

    static std::unordered_map<uint32_t, uint32_t> units;
    followed = &units;
    QueryPerformanceFrequency(&frequency);
    pathLimit = argumentValue(L"-pathlimit");
    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
