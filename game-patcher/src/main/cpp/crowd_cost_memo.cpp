#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "crowd_cost_math.h"
#include "crowd_cost_memo.h"
#include "features.h"
#include "patch_set.h"
#include "stats.h"

// The pathfinder prices every cell its A* search opens by the crowd cost (0x006ED21E): it walks a window of cells around
// the cell and, for every unit standing in the window, asks three questions about that unit:
//   - how the mover relates to it (0x0068D7AB),
//   - its priority, a sum over its attribute modifiers (0x00663F48, once for it and once for the mover),
//   - how fast it can move (0x005E3F49).
// A unit stands in the windows of many neighbouring cells, so a search asks the same questions about the same unit again
// and again. (Each cell is priced only once per search, so memoising the crowd cost itself finds nothing to reuse.)
//
// The answers read the units, their modifiers and the terrain, none of which change while a search runs, so within one
// search the same question always gets the same answer. This keeps them in small hash tables keyed by the arguments.
//
// The tables are dropped whenever a search ends: every search finishes through the open and closed list cleanup
// (0x006F5519), and a counter in front of it makes all earlier entries stale. Nothing is kept from one search to the
// next, so a search that only one machine runs (the UI asks the pathfinder for the cursor feedback) cannot leave
// anything behind that another search could read.
//
// With statistics on, every 16th hit also runs the original function and compares, and the result of the original wins
// if they ever differ. The crowd cost is timed with the time stamp counter, and a control word lets a measuring tool turn
// the memo off while the game runs.

using namespace crowd_cost;
using namespace patching;

namespace {

    constexpr uintptr_t CROWD_COST = 0x006ED21E;
    constexpr uintptr_t RELATIONSHIP = 0x0068D7AB;
    constexpr uintptr_t PRIORITY = 0x00663F48;
    constexpr uintptr_t SPEED = 0x005E3F49;

    constexpr uintptr_t CROWD_COST_CALL_IN_EXPANSION = 0x006F9E7D;
    constexpr uintptr_t CROWD_COST_CALL_IN_DESTINATION_ADJUSTMENT = 0x006F73D6;
    constexpr uintptr_t RELATIONSHIP_CALL = 0x006ED2EC;
    constexpr uintptr_t PRIORITY_CALL_OF_OTHER = 0x006ED325;
    constexpr uintptr_t PRIORITY_CALL_OF_MOVER = 0x006ED332;
    constexpr uintptr_t SPEED_CALL_OF_OTHER = 0x006ED414;

    constexpr uintptr_t CELL_LOOKUP = 0x005E2E9C;
    constexpr uintptr_t UNIT_FLAG = 0x0068B47F;
    constexpr uintptr_t CELL_CENTER = 0x006ED049;
    constexpr uintptr_t INITIAL_TRAVEL_TIME = 0x00BD19DC;

    constexpr uintptr_t CROWD_COST_CALL_IN_ESCAPE_SEARCH = 0x006FB0B0;
    constexpr uintptr_t CROWD_COST_CALL_IN_MELEE_SEARCH = 0x006F702A;

    constexpr uintptr_t QUEUE_PROCESSING = 0x006F2364;
    constexpr uintptr_t QUEUE_PROCESSING_CALL = 0x0062E69F;

    constexpr uintptr_t CLEANUP = 0x006F5519;
    constexpr uintptr_t CLEANUP_AFTER_CALL = 0x006F5522;
    constexpr uintptr_t CLEANUP_CALLEE = 0x006F4B1A;

    typedef int (__thiscall *CrowdCost)(void *self, int mover, int node, const int *cell, int radius, int extent, int flag);
    typedef int (__thiscall *Relationship)(int mover, int other);
    typedef int (__fastcall *Priority)(int component, int unused, int flag);
    typedef float (__thiscall *Speed)(int locomotor, int unit);
    typedef int (__thiscall *CellLookup)(void *self, int layer, int x, int y);
    typedef uint8_t (__fastcall *UnitFlag)(int unit);
    typedef float *(__cdecl *CellCenter)(float *out, int mover, const int *cell, int flag);

    struct Entry {
        uint32_t epoch;
        uint32_t first;
        uint32_t second;
        uint32_t result;
    };

    constexpr size_t TABLE_SIZE = 8192;
    Entry relationships[TABLE_SIZE];
    Entry priorities[TABLE_SIZE];
    Entry speeds[TABLE_SIZE];
    volatile uint32_t epoch = 1;

    Stats *counters() {
        return features::STATS ? stats() : nullptr;
    }

    bool memoizing(const Stats *statistics) {
        return statistics == nullptr || statistics->control >= 2;
    }

    Entry &slot(Entry *table, const uint32_t first, const uint32_t second) {
        uint32_t hash = first * 0x9E3779B1u;
        hash ^= second * 0x85EBCA6Bu;
        hash ^= hash >> 15;
        return table[hash & (TABLE_SIZE - 1)];
    }

    // Looks the question up and runs `compute` on a miss. With statistics on, every 16th hit is checked against a fresh
    // answer, which wins on a difference.
    template<typename Compute>
    uint32_t remembered(Entry *table, const uint32_t first, const uint32_t second, Compute compute) {

        Stats *statistics = counters();
        if (!memoizing(statistics)) {
            return compute();
        }

        Entry &entry = slot(table, first, second);
        const uint32_t current = epoch;
        if (statistics != nullptr) {
            statistics->objectMemoLookups++;
        }
        if (entry.epoch == current && entry.first == first && entry.second == second) {
            uint32_t result = entry.result;
            if (statistics != nullptr && (++statistics->crowdCostHits & 15) == 0) {
                const uint32_t again = compute();
                statistics->crowdCostChecks++;
                if (again != result) {
                    statistics->crowdCostMismatches++;
                    result = again;
                }
            }
            return result;
        }

        const uint32_t result = compute();
        entry.epoch = current;
        entry.first = first;
        entry.second = second;
        entry.result = result;
        return result;
    }

    // thiscall with one stack argument, callee cleans up: __fastcall with a dummy second parameter has the same layout.
    int __fastcall relationshipMemo(int mover, int, int other) {
        return static_cast<int>(remembered(relationships, mover, other, [&] {
            return static_cast<uint32_t>(reinterpret_cast<Relationship>(RELATIONSHIP)(mover, other));
        }));
    }

    // The function takes a flag on the stack and pops it, but never reads it, so the component alone is the question.
    int __fastcall priorityMemo(int component, int, int flag) {
        return static_cast<int>(remembered(priorities, component, 0, [&] {
            return static_cast<uint32_t>(reinterpret_cast<Priority>(PRIORITY)(component, 0, flag));
        }));
    }

    // The speed comes back in st(0) as a float widened to the x87 format, which a float return reproduces exactly.
    float __fastcall speedMemo(int locomotor, int, int unit) {
        const uint32_t bits = remembered(speeds, locomotor, unit, [&] {
            const float speed = reinterpret_cast<Speed>(SPEED)(locomotor, unit);
            uint32_t pattern;
            memcpy(&pattern, &speed, sizeof(pattern));
            return pattern;
        });
        float speed;
        memcpy(&speed, &bits, sizeof(speed));
        return speed;
    }

    // Times the queue processing and keeps the last calls in a ring for the measuring tool. Only installed with statistics.
    void __fastcall queueProcessingTimed(int self) {
        Stats *statistics = counters();
        const volatile uint32_t *queues = reinterpret_cast<const volatile uint32_t *>(self);
        QueueRecord record;
        record.before[0] = queues[0x1D1E8 / 4];
        record.before[1] = queues[0x1D1EC / 4];
        record.before[2] = queues[0x1C9E0 / 4];
        record.before[3] = queues[0x1C9E4 / 4];
        const uint64_t start = __rdtsc();
        reinterpret_cast<void (__fastcall *)(int)>(QUEUE_PROCESSING)(self);
        record.cycles = static_cast<uint32_t>(__rdtsc() - start);
        record.cells = queues[0x3C / 4];
        record.after[0] = queues[0x1D1E8 / 4];
        record.after[1] = queues[0x1D1EC / 4];
        record.after[2] = queues[0x1C9E0 / 4];
        record.after[3] = queues[0x1C9E4 / 4];
        uint64_t cycles = (static_cast<uint64_t>(statistics->queueCyclesHigh) << 32) | statistics->queueCyclesLow;
        cycles += record.cycles;
        statistics->queueCyclesLow = static_cast<uint32_t>(cycles);
        statistics->queueCyclesHigh = static_cast<uint32_t>(cycles >> 32);
        statistics->queue[statistics->queueWrites % 64] = record;
        statistics->queueWrites++;
    }

    // What a unit in a cell window adds to the crowd cost of a cell, as far as it does not depend on the cell: nothing, a
    // fixed 8 (before the shift), or 4 when the mover would get to the cell centre later than the unit does.
    enum Kind : uint8_t {
        IGNORED = 0,
        FIXED = 1,
        BY_DISTANCE = 2
    };

    struct UnitRecord {
        uint32_t epoch;
        uint32_t mover;
        uint32_t other;
        uint32_t flag;
        Kind kind;
        float x;
        float y;
        float speed;
    };

    UnitRecord unitRecords[TABLE_SIZE];

    int readInt(const int address) {
        return *reinterpret_cast<const int *>(address);
    }

    float readFloat(const int address) {
        return *reinterpret_cast<const float *>(address);
    }

    // The questions the game's loop asks about a unit, in its order, for one mover and one unit. `flag` is the last
    // argument of the crowd cost (a byte, 1 from the two search loops).
    void classify(UnitRecord &record, const int mover, const int other, const int flag) {

        record.kind = IGNORED;
        const bool allied = reinterpret_cast<Relationship>(RELATIONSHIP)(mover, other) == 0;
        if (allied && flag != 0) {
            return;
        }
        const int moverAi = readInt(mover + 0x260);
        const int otherAi = readInt(other + 0x260);
        if (moverAi == 0 || otherAi == 0) {
            return;
        }
        const auto priority = reinterpret_cast<Priority>(PRIORITY);
        const uint32_t otherPriority = static_cast<uint32_t>(priority(otherAi, 0, allied));
        const uint32_t moverPriority = static_cast<uint32_t>(priority(moverAi, 0, allied));
        if (moverPriority > otherPriority) {
            return;
        }
        if (reinterpret_cast<UnitFlag>(UNIT_FLAG)(other) != 0) {
            record.kind = FIXED;
            return;
        }
        const int moverLocomotor = readInt(moverAi + 0x1F0);
        const int otherLocomotor = readInt(otherAi + 0x1F0);
        if (moverLocomotor == 0 || otherLocomotor == 0) {
            record.kind = FIXED;
            return;
        }
        record.kind = BY_DISTANCE;
        record.x = readFloat(other + 0x38);
        record.y = readFloat(other + 0x3C);
        record.speed = reinterpret_cast<Speed>(SPEED)(otherLocomotor, other);
    }

    const UnitRecord &recordOf(const int mover, const int other, const int flag) {

        UnitRecord &record = unitRecords[((static_cast<uint32_t>(mover) * 0x9E3779B1u) ^ (static_cast<uint32_t>(other) * 0x85EBCA6Bu) ^ (static_cast<uint32_t>(flag) * 0xC2B2AE35u)) >> 19 & (TABLE_SIZE - 1)];
        const uint32_t current = epoch;
        if (record.epoch == current && record.mover == static_cast<uint32_t>(mover) && record.other == static_cast<uint32_t>(other)
            && record.flag == static_cast<uint32_t>(flag)) {
            return record;
        }
        classify(record, mover, other, flag);
        record.epoch = current;
        record.mover = mover;
        record.other = other;
        record.flag = flag;
        return record;
    }

    template<bool Wide>
    int crowdCostOf(void *self, const int mover, const int node, const int *cell, const int radius, const int extent, const int flag, bool &ok) {

        if (node == 0) {
            return 0;
        }
        const int shift = radius < 4 ? (radius > 1 ? 1 : 0) : 2;
        const int layer = static_cast<int>((static_cast<uint32_t>(readInt(node + 0xC)) >> 4) & 0x3F);
        const int fixedShare = 8 >> shift;
        const int distanceShare = 4 >> shift;
        const int unitFlag = flag & 0xFF;
        const auto lookup = reinterpret_cast<CellLookup>(CELL_LOOKUP);

        int total = 0;
        float travelTime = readFloat(INITIAL_TRAVEL_TIME);
        float centre[3];
        bool haveCentre = false;

        for (int x = cell[0] - radius; x < cell[0] + extent; x++) {
            for (int y = cell[1] - radius; y < cell[1] + extent; y++) {
                const int cellInfo = lookup(self, layer, x, y);
                if (cellInfo == 0) {
                    continue;
                }
                const int units = readInt(cellInfo);
                if (units == 0) {
                    continue;
                }
                for (int node_ = readInt(units + 0x14); node_ != 0; node_ = readInt(node_)) {
                    const int other = readInt(node_ + 8);
                    if (other == mover || readInt(other + 0x27C) == mover) {
                        continue;
                    }
                    const UnitRecord &record = recordOf(mover, other, unitFlag);
                    if (record.kind == IGNORED) {
                        continue;
                    }
                    if (record.kind == FIXED) {
                        total += fixedShare;
                        continue;
                    }

                    if (!haveCentre) {
                        reinterpret_cast<CellCenter>(CELL_CENTER)(centre, mover, cell, 1);
                        haveCentre = true;
                    }
                    if (travelTime < 0.0f) {
                        const int moverLocomotor = readInt(readInt(mover + 0x260) + 0x1F0);
                        const float distance = distanceTo<Wide>(readFloat(mover + 0x38), readFloat(mover + 0x3C), centre[0], centre[1], ok);
                        const float speed = reinterpret_cast<Speed>(SPEED)(moverLocomotor, mover);
                        if (!usableSpeed(speed)) {
                            ok = false;
                        }
                        travelTime = static_cast<float>(timeFor<Wide>(distance, speed));
                    }
                    const float distance = distanceTo<Wide>(record.x, record.y, centre[0], centre[1], ok);
                    if (!usableSpeed(record.speed)) {
                        ok = false;
                    }
                    if (static_cast<double>(travelTime) > timeFor<Wide>(distance, record.speed)) {
                        total += distanceShare;
                    }
                }
            }
        }
        return total;
    }

    // 0 for 24 bits, 1 for 53 bits, -1 for the other settings, whose rounding this does not reproduce
    int precision() {
        unsigned short controlWord;
        __asm fnstcw controlWord
        switch (controlWord & 0x300) {
            case 0x000:
                return 0;
            case 0x200:
                return 1;
            default:
                return -1;
        }
    }

    // Stands in for the four calls of the crowd cost. The game's function stays the reference: where this cannot reproduce
    // it, it runs, and with statistics on every 32nd answer is compared with it.
    int __fastcall crowdCost(void *self, void *, int mover, int node, const int *cell, int radius, int extent, int flag) {

        Stats *statistics = counters();
        const uint64_t start = statistics != nullptr ? __rdtsc() : 0;
        const auto original = reinterpret_cast<CrowdCost>(CROWD_COST);

        int result;
        const bool rewriting = statistics == nullptr ? features::CROWD_COST_REWRITE : statistics->control >= 3;
        const int mode = rewriting ? precision() : -1;
        if (mode < 0) {
            if (statistics != nullptr && rewriting) {
                statistics->crowdRewriteFallbacks++;
            }
            result = original(self, mover, node, cell, radius, extent, flag);
        } else {
            bool ok = true;
            result = mode == 0 ? crowdCostOf<false>(self, mover, node, cell, radius, extent, flag, ok)
                               : crowdCostOf<true>(self, mover, node, cell, radius, extent, flag, ok);
            if (!ok) {
                if (statistics != nullptr) {
                    statistics->crowdRewriteFallbacks++;
                }
                result = original(self, mover, node, cell, radius, extent, flag);
            } else if (statistics != nullptr) {
                if ((++statistics->crowdRewriteCalls & 31) == 0) {
                    const int again = original(self, mover, node, cell, radius, extent, flag);
                    statistics->crowdRewriteChecks++;
                    if (again != result) {
                        statistics->crowdRewriteMismatches++;
                        result = again;
                    }
                }
            }
        }

        if (statistics != nullptr) {
            uint64_t cycles = (static_cast<uint64_t>(statistics->crowdCostCyclesHigh) << 32) | statistics->crowdCostCyclesLow;
            cycles += __rdtsc() - start;
            statistics->crowdCostCyclesLow = static_cast<uint32_t>(cycles);
            statistics->crowdCostCyclesHigh = static_cast<uint32_t>(cycles >> 32);
            statistics->crowdCostCalls++;
        }
        return result;
    }
}

bool applyCrowdCostMemo() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    // The cleanup starts with push esi / push edi / mov esi, ecx / call CLEANUP_CALLEE. The call is position dependent, so
    // the cave runs those four instructions itself after counting the search as over.
    const uintptr_t cleanupHook = cave.here();
    cave.increment(reinterpret_cast<uintptr_t>(&epoch));
    const BYTE displaced[4] = {0x56, 0x57, 0x8B, 0xF1};
    cave.emit(displaced);
    cave.call(CLEANUP_CALLEE);
    cave.jmp(CLEANUP_AFTER_CALL);

    static PatchSet set;
    set.reset();

    const BYTE cleanupHead[9] = {0x56, 0x57, 0x8B, 0xF1, 0xE8, 0xF8, 0xF5, 0xFF, 0xFF};
    set.hook(CLEANUP, cleanupHead, cleanupHook);
    const BYTE cleanupNext[3] = {0x8B, 0x4E, 0x34};
    set.expect(CLEANUP_AFTER_CALL, cleanupNext);

    // call 0x0068D7AB
    const BYTE relationshipCall[5] = {0xE8, 0xBA, 0x04, 0xFA, 0xFF};
    set.redirectCall(RELATIONSHIP_CALL, relationshipCall, reinterpret_cast<uintptr_t>(&relationshipMemo));

    // call 0x00663F48, for the unit in the window and for the mover
    const BYTE priorityCallOfOther[5] = {0xE8, 0x1E, 0x6C, 0xF7, 0xFF};
    set.redirectCall(PRIORITY_CALL_OF_OTHER, priorityCallOfOther, reinterpret_cast<uintptr_t>(&priorityMemo));
    const BYTE priorityCallOfMover[5] = {0xE8, 0x11, 0x6C, 0xF7, 0xFF};
    set.redirectCall(PRIORITY_CALL_OF_MOVER, priorityCallOfMover, reinterpret_cast<uintptr_t>(&priorityMemo));

    // call 0x005E3F49, the speed of the unit in the window
    const BYTE speedCall[5] = {0xE8, 0x30, 0x6B, 0xEF, 0xFF};
    set.redirectCall(SPEED_CALL_OF_OTHER, speedCall, reinterpret_cast<uintptr_t>(&speedMemo));

    if (features::STATS) {
        const BYTE queueCall[5] = {0xE8, 0xC0, 0x3C, 0x0C, 0x00};
        set.redirectCall(QUEUE_PROCESSING_CALL, queueCall, reinterpret_cast<uintptr_t>(&queueProcessingTimed));
    }
    if (features::STATS || features::CROWD_COST_REWRITE) {
        // the four calls of the crowd cost
        const BYTE expansionCall[5] = {0xE8, 0x9C, 0x33, 0xFF, 0xFF};
        set.redirectCall(CROWD_COST_CALL_IN_EXPANSION, expansionCall, reinterpret_cast<uintptr_t>(&crowdCost));
        const BYTE adjustmentCall[5] = {0xE8, 0x43, 0x5E, 0xFF, 0xFF};
        set.redirectCall(CROWD_COST_CALL_IN_DESTINATION_ADJUSTMENT, adjustmentCall, reinterpret_cast<uintptr_t>(&crowdCost));
        const BYTE escapeCall[5] = {0xE8, 0x69, 0x21, 0xFF, 0xFF};
        set.redirectCall(CROWD_COST_CALL_IN_ESCAPE_SEARCH, escapeCall, reinterpret_cast<uintptr_t>(&crowdCost));
        const BYTE meleeCall[5] = {0xE8, 0xEF, 0x61, 0xFF, 0xFF};
        set.redirectCall(CROWD_COST_CALL_IN_MELEE_SEARCH, meleeCall, reinterpret_cast<uintptr_t>(&crowdCost));
    }

    // The functions the calls reach must still start the way the wrappers assume.
    const BYTE crowdCostHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30};
    set.expect(CROWD_COST, crowdCostHead);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
