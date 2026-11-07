#include <windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstdint>

#include "collision_pair_table.h"
#include "game_memory.h"
#include "instrumentation/call_timing.h"
#include "instrumentation/pathfinder_timing.h"
#include "instrumentation/stats.h"
#include "patch_set.h"

// Scope timers (debug mode): times functions of the game (inclusive) and keeps a record of every call of
// AdjustDestination and of the message dispatcher. A cave notes the time on entry, swaps the return address on the stack for
// the address of a shared exit stub and, when the function returns, takes the time again and jumps to the real return
// address. Functions that start with the compiler's exception frame helper (mov eax, handler / call 0x00A3CEF0) are hooked
// behind it, where the return address is at [ebp + 4]; the others at their first instruction, where it is at [esp].

using namespace instrumentation;
using namespace patching;

namespace {

    constexpr size_t MAX_DEPTH = 64;
    constexpr size_t MAX_DEFINITIONS = 48;

    constexpr uint32_t ADJUST = 0;
    constexpr uint32_t BROAD_PHASE = 6;
    constexpr uint32_t DISPATCH = 8;
    constexpr uint32_t CELL_CHECK = 16;
    constexpr uint32_t PATH_EXISTS = 19;
    constexpr uint32_t OCCUPANCY = 20;
    constexpr uint32_t RELATIONSHIP = 21;
    constexpr uint32_t CHECK_DESTINATION = 18;
    // the scopes whose time AdjustDestination's records break down
    constexpr uint32_t ADJUST_SUB[ADJUST_SUBSCOPES] = {1, 2, 3, 4, 5, PATH_EXISTS, CHECK_DESTINATION};

    // the collision broad phase: its sweep (29), the contact callbacks over the active pairs (30), the pair clean up (31) and
    // counted: pair counter increments (32) and decrements (33) in the first pair table, inserts into (34) and removals from (35)
    // the second table, proxy bound updates (36)
    constexpr uint32_t SAMPLE_EVERY = 32;

    struct Definition {
        uint32_t firstScope;
        uint32_t scopeCount;
        // the index of the argument whose value picks the scope (first + value), or -1
        int32_t tagArgument;
    };

    struct Entry {
        const uint32_t *returnSlot;
        uint32_t returnAddress;
        uint32_t scope;
        uint32_t messageType;
        uint64_t start;
        uint64_t adjustCycles;
        uint32_t adjustCalls;
        uint64_t sub[ADJUST_SUBSCOPES];
        uint32_t cellChecks;
        uint32_t pathExistCalls;
        uint32_t relationCalls;
        uint32_t object;
    };

    Definition definitions[MAX_DEFINITIONS];
    uint32_t definitionCount = 0;
    Entry entries[MAX_DEPTH];
    size_t depth = 0;
    uint32_t ownerThread = 0;
    uintptr_t exitAddress = 0;

    uint64_t scopeCycles(const Stats *statistics, const size_t scope) {
        return (static_cast<uint64_t>(statistics->scopes[scope].cyclesHigh) << 32) | statistics->scopes[scope].cyclesLow;
    }

    uint32_t narrow(const uint64_t cycles) {
        return cycles > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(cycles);
    }

    // The broad phase object has the proxies as a list at +0 (next at +0xC), the number of axes at +0xC068 and two chained hash
    // tables of pair nodes (next at +0x30, the first one counts the axes on which the pair overlaps at +0x10): 11131 buckets at
    // +0x18 (or the bigger one behind the object when the game-patcher dll's broad phase table patch is applied, see
    // collision_pair_table.cpp there) and 1171 buckets at +0xAE10. Walks both and fills the sample.
    constexpr uint32_t TABLE_B = 0xAE10;
    constexpr uint32_t BUCKETS_B = 0x493;
    constexpr uint32_t WALK_LIMIT = 4000000;

    size_t chainBucket(const uint32_t length) {
        return length <= 2 ? length : length <= 4 ? 3 : length <= 8 ? 4 : length <= 16 ? 5 : length <= 32 ? 6 : 7;
    }

    void measureTable(const uint8_t *object, const uint32_t table, const uint32_t buckets, const uint32_t axes, uint32_t &nodes,
                      uint32_t &longest, uint32_t &steps, uint32_t &overlapping, uint32_t (&histogram)[BROAD_PHASE_BUCKETS], uint32_t (&states)[3]) {
        nodes = 0;
        longest = 0;
        steps = 0;
        overlapping = 0;
        states[0] = states[1] = states[2] = 0;
        for (size_t i = 0; i < BROAD_PHASE_BUCKETS; i++) {
            histogram[i] = 0;
        }
        uint32_t walked = 0;
        for (uint32_t bucket = 0; bucket < buckets; bucket++) {
            uint32_t length = 0;
            for (uint32_t node = field<uint32_t>(object, table + bucket * 4); node != 0 && walked < WALK_LIMIT; node = field<uint32_t>(reinterpret_cast<const uint8_t *>(node), 0x30)) {
                length++;
                walked++;
                const uint32_t counter = field<uint32_t>(reinterpret_cast<const uint8_t *>(node), 0x10);
                if (counter == axes) {
                    overlapping++;
                }
                if (counter < 3) {
                    states[counter]++;
                }
            }
            histogram[chainBucket(length)]++;
            nodes += length;
            steps += length * (length + 1) / 2;
            if (length > longest) {
                longest = length;
            }
        }
    }

    void measureBroadPhase(const uint8_t *object, BroadPhaseSample &sample, const bool biggerTable) {
        sample.axes = field<uint32_t>(object, 0xC068);
        sample.proxies = 0;
        uint32_t walked = 0;
        for (uint32_t proxy = field<uint32_t>(object, 0); proxy != 0 && walked < WALK_LIMIT; proxy = field<uint32_t>(reinterpret_cast<const uint8_t *>(proxy), 0xC)) {
            sample.proxies++;
            walked++;
        }
        uint32_t unusedStates[3];
        measureTable(object, biggerTable ? collision_pair_table::TABLE : collision_pair_table::ORIGINAL_TABLE, biggerTable ? collision_pair_table::BUCKETS : collision_pair_table::ORIGINAL_BUCKETS, sample.axes, sample.nodesA, sample.maxChainA, sample.stepsA, sample.overlapping, sample.histogramA, unusedStates);
        uint32_t overlappingB;
        measureTable(object, TABLE_B, BUCKETS_B, 0xFFFFFFFF, sample.nodesB, sample.maxChainB, sample.stepsB, overlappingB, sample.histogramB, sample.statesB);
    }

    void __cdecl entered(uint32_t *returnSlot, const uint32_t definitionIndex) {
        // the entries are a stack for one thread; the timed functions run on the game's main thread
        if (__readfsdword(0x24) != ownerThread) {
            return;
        }
        // a timed function that tail-jumped here shares this slot and already made it return to the exit stub: this call is timed
        // as part of that one
        if (*returnSlot == exitAddress) {
            return;
        }
        // An open entry whose function an exception went through lies in stack that was unwound past: its slot is at or below this
        // call's (a later call may reuse it), or the arguments of this call overwrote it. Every function still running has its slot
        // above and still returning to the exit stub. Dropping them keeps the slots falling from bottom to top, and no stale slot is
        // left inside the argument bytes left() counts from.
        while (depth > 0 && (entries[depth - 1].returnSlot <= returnSlot || *entries[depth - 1].returnSlot != exitAddress)) {
            depth--;
        }
        if (depth >= MAX_DEPTH) {
            return;
        }
        const Stats *statistics = stats();
        const Definition &definition = definitions[definitionIndex];
        uint32_t scope = definition.firstScope;
        if (definition.tagArgument >= 0) {
            const uint32_t tag = returnSlot[1 + definition.tagArgument];
            scope += tag < definition.scopeCount ? tag : definition.scopeCount - 1;
        }

        Entry &entry = entries[depth++];
        entry.returnSlot = returnSlot;
        entry.returnAddress = *returnSlot;
        entry.scope = scope;
        entry.messageType = 0;
        if (scope == BROAD_PHASE) {
            entry.object = returnSlot[-1];
        } else if (scope == ADJUST) {
            entry.messageType = returnSlot[4] != 0 ? 1 : 0;
            for (size_t i = 0; i < ADJUST_SUBSCOPES; i++) {
                entry.sub[i] = scopeCycles(statistics, ADJUST_SUB[i]);
            }
            entry.cellChecks = statistics->scopes[CELL_CHECK].calls;
            entry.pathExistCalls = statistics->scopes[PATH_EXISTS].calls;
            entry.relationCalls = statistics->scopes[RELATIONSHIP].calls;
        } else if (scope == DISPATCH) {
            const uint32_t message = returnSlot[1];
            entry.messageType = message != 0 ? *reinterpret_cast<const uint32_t *>(message + 0x10) : 0;
            entry.adjustCycles = scopeCycles(statistics, ADJUST);
            entry.adjustCalls = statistics->scopes[ADJUST].calls;
        }
        entry.start = __rdtsc();
        *returnSlot = static_cast<uint32_t>(exitAddress);
    }

    // `highestSlot` is the stack pointer the exit stub was entered with, minus 4: the returning function's slot, or above it by the
    // arguments its ret popped. A timed function that an exception unwound through never reached the stub, so its entry is still
    // open above the returning one, with a slot below it; callers still running have their slots above highestSlot. The returning
    // function's entry is therefore the oldest one at or below highestSlot, and the entries above it are dropped.
    uint32_t __cdecl left(const uint32_t *highestSlot) {
        const uint64_t end = __rdtsc();
        // unreachable, every stub run has its entry; returning 0 crashes at once instead of reading before the entries
        if (depth == 0) {
            return 0;
        }
        while (depth > 1 && entries[depth - 2].returnSlot <= highestSlot) {
            depth--;
        }
        const Entry &entry = entries[--depth];
        Stats *statistics = stats();
        const uint64_t cycles = end - entry.start;

        ScopeStat &scope = statistics->scopes[entry.scope];
        const uint64_t total = ((static_cast<uint64_t>(scope.cyclesHigh) << 32) | scope.cyclesLow) + cycles;
        scope.calls++;
        scope.cyclesLow = static_cast<uint32_t>(total);
        scope.cyclesHigh = static_cast<uint32_t>(total >> 32);
        if (narrow(cycles) > scope.maxCycles) {
            scope.maxCycles = narrow(cycles);
        }

        if (entry.scope == BROAD_PHASE && scope.calls % SAMPLE_EVERY == 0 && entry.object != 0) {
            // the walk is not part of the game's time: the timers still open are paused for it
            const uint64_t sampleStart = __rdtsc();
            BroadPhaseSample &sample = statistics->broadPhaseSamples[statistics->broadPhaseSampleWrites % BROAD_PHASE_RING];
            measureBroadPhase(reinterpret_cast<const uint8_t *>(entry.object), sample, collisionPairTableApplied());
            sample.call = scope.calls;
            statistics->broadPhaseSampleWrites++;
            const uint64_t sampleCycles = __rdtsc() - sampleStart;
            for (size_t i = 0; i < depth; i++) {
                entries[i].start += sampleCycles;
            }
            pauseCallTimers(sampleCycles);
        }

        if (entry.scope == ADJUST) {
            AdjustRecord &record = statistics->adjustRecords[statistics->adjustRecordWrites % ADJUST_RING];
            record.caller = entry.returnAddress;
            record.cycles = narrow(cycles);
            record.groupDestination = entry.messageType;
            record.cellChecks = statistics->scopes[CELL_CHECK].calls - entry.cellChecks;
            record.pathExistCalls = statistics->scopes[PATH_EXISTS].calls - entry.pathExistCalls;
            record.relationCalls = statistics->scopes[RELATIONSHIP].calls - entry.relationCalls;
            for (size_t i = 0; i < ADJUST_SUBSCOPES; i++) {
                record.sub[i] = narrow(scopeCycles(statistics, ADJUST_SUB[i]) - entry.sub[i]);
            }
            statistics->adjustRecordWrites++;
        } else if (entry.scope == DISPATCH) {
            MessageRecord &record = statistics->messageRecords[statistics->messageRecordWrites % MESSAGE_RING];
            record.type = entry.messageType;
            record.cycles = narrow(cycles);
            record.adjustCycles = narrow(scopeCycles(statistics, ADJUST) - entry.adjustCycles);
            record.adjustCalls = statistics->scopes[ADJUST].calls - entry.adjustCalls;
            statistics->messageRecordWrites++;
        }
        return entry.returnAddress;
    }

    // sub esp, 4 / push eax / push ecx / push edx / lea eax, [esp + 0xC] / push eax / call left / add esp, 4 / mov [esp + 0xC], eax /
    // pop edx / pop ecx / pop eax / ret: the function's results (eax, edx, st0) survive, and so does ecx, which the game's own
    // functions rely on across calls (the broad phase's clean up, sweep and contact steps are called one after the other with the
    // same ecx). left gets the stack pointer of the stub's entry minus 4. The ret takes the real return address from the slot and
    // leaves the stack as the caller expects after the function's own ret.
    uintptr_t emitExit(Cave &cave) {
        const uintptr_t address = cave.here();
        const BYTE save[5] = {0x83, 0xEC, 0x04, 0x50, 0x51};
        cave.emit(save);
        cave.emit8(0x52);
        const BYTE pushSlot[5] = {0x8D, 0x44, 0x24, 0x0C, 0x50};
        cave.emit(pushSlot);
        cave.call(reinterpret_cast<uintptr_t>(&left));
        const BYTE finish[11] = {0x83, 0xC4, 0x04, 0x89, 0x44, 0x24, 0x0C, 0x5A, 0x59, 0x58, 0xC3};
        cave.emit(finish);
        return address;
    }

    // push ecx / push edx / lea eax, [the return address] / push definition / push eax / call entered / add esp, 8 /
    // pop edx / pop ecx / the instructions the hook replaced / jmp back
    template<size_t N>
    uintptr_t emitEntry(Cave &cave, const uint32_t definition, const bool framed, const BYTE (&displaced)[N], const uintptr_t resume) {
        const uintptr_t address = cave.here();
        cave.emit8(0x51);
        cave.emit8(0x52);
        if (framed) {
            const BYTE lea[3] = {0x8D, 0x45, 0x04};
            cave.emit(lea);
        } else {
            const BYTE lea[4] = {0x8D, 0x44, 0x24, 0x08};
            cave.emit(lea);
        }
        cave.emit8(0x68);
        cave.emit32(definition);
        cave.emit8(0x50);
        cave.call(reinterpret_cast<uintptr_t>(&entered));
        const BYTE restore[5] = {0x83, 0xC4, 0x08, 0x5A, 0x59};
        cave.emit(restore);
        cave.emit(displaced);
        cave.jmp(resume);
        return address;
    }

    uint32_t define(const uint32_t firstScope, const uint32_t scopeCount, const int32_t tagArgument) {
        definitions[definitionCount] = {firstScope, scopeCount, tagArgument};
        return definitionCount++;
    }

    // `head` are the instructions at the start of the function that the hook replaces.
    template<size_t N>
    void timeFunction(PatchSet &set, Cave &cave, const uintptr_t address, const BYTE (&head)[N], const uint32_t firstScope,
                      const uint32_t scopeCount = 1, const int32_t tagArgument = -1) {
        const uint32_t definition = define(firstScope, scopeCount, tagArgument);
        set.hook(address, head, emitEntry(cave, definition, false, head, address + N));
    }

    // For functions called too often to time: the first instructions are replaced by a jump to a cave that counts the call
    // and runs them. Nothing on the stack changes, so the profiler still sees the callers.
    template<size_t N>
    void countFunction(PatchSet &set, Cave &cave, const uintptr_t address, const BYTE (&head)[N], const uint32_t scope) {
        const uintptr_t entry = cave.here();
        cave.increment(reinterpret_cast<uintptr_t>(&stats()->scopes[scope].calls));
        cave.emit(head);
        cave.jmp(address + N);
        set.hook(address, head, entry);
    }

    // `prologue` is the exception frame helper call at the start of the function, `body` the instructions behind it.
    template<size_t P, size_t N>
    void timeFramedFunction(PatchSet &set, Cave &cave, const uintptr_t address, const BYTE (&prologue)[P], const BYTE (&body)[N],
                            const uint32_t firstScope, const uint32_t scopeCount = 1, const int32_t tagArgument = -1) {
        const uint32_t definition = define(firstScope, scopeCount, tagArgument);
        set.expect(address, prologue);
        set.hook(address + P, body, emitEntry(cave, definition, true, body, address + P + N));
    }
}

bool instrumentation::applyPathfinderTiming() {

    if (stats() == nullptr) {
        return false;
    }

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);
    exitAddress = emitExit(cave);
    // applied in DllMain, on the thread that runs the game
    ownerThread = GetCurrentThreadId();

    static PatchSet set;
    set.reset();
    definitionCount = 0;

    // AdjustDestination: push ebp / mov ebp, esp / sub esp, 0x9C
    const BYTE adjustHead[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x9C, 0x00, 0x00, 0x00};
    timeFunction(set, cave, 0x006FE456, adjustHead, ADJUST);
    // cmp byte [0x00DE9185], 0
    const BYTE adjustBody[7] = {0x80, 0x3D, 0x85, 0x91, 0xDE, 0x00, 0x00};
    set.expect(0x006FE45F, adjustBody);

    // the searches AdjustDestination starts: ring search around the destination (1), the same with a group destination (2),
    // the group's path tightening A* (3), the group destination step (4) and the placement of the search start (5)
    const BYTE ringHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C};
    timeFunction(set, cave, 0x006FA2A4, ringHead, 1);
    timeFunction(set, cave, 0x006FA560, ringHead, 2);
    const BYTE tightenHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x50};
    timeFunction(set, cave, 0x006F70D5, tightenHead, 3);
    const BYTE groupHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34};
    timeFunction(set, cave, 0x006FDDE5, groupHead, 4);
    const BYTE placementHead[6] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08};
    timeFunction(set, cave, 0x006EE84E, placementHead, 5);

    // the collision broad phase update: push esi / mov esi, ecx / cmp dword [esi], 0
    const BYTE broadPhaseHead[6] = {0x56, 0x8B, 0xF1, 0x83, 0x3E, 0x00};
    timeFunction(set, cave, 0x00B6E6B0, broadPhaseHead, 6);

    // mov eax, handler / call 0x00A3CEF0, then sub esp, imm32
    const BYTE visibilityPrologue[10] = {0xB8, 0x2C, 0x84, 0xB8, 0x00, 0xE8, 0x06, 0x48, 0x3B, 0x00};
    const BYTE visibilityBody[6] = {0x81, 0xEC, 0xF4, 0x00, 0x00, 0x00};
    timeFramedFunction(set, cave, 0x006886E0, visibilityPrologue, visibilityBody, 7);

    const BYTE dispatchPrologue[10] = {0xB8, 0x43, 0x31, 0xB9, 0x00, 0xE8, 0xA9, 0x34, 0x2C, 0x00};
    const BYTE dispatchBody[6] = {0x81, 0xEC, 0x10, 0x02, 0x00, 0x00};
    timeFramedFunction(set, cave, 0x00779A3D, dispatchPrologue, dispatchBody, DISPATCH);

    const BYTE updatePrologue[10] = {0xB8, 0xDA, 0x41, 0xB8, 0x00, 0xE8, 0xFE, 0xE9, 0x40, 0x00};
    const BYTE updateBody[6] = {0x81, 0xEC, 0x74, 0x01, 0x00, 0x00};
    timeFramedFunction(set, cave, 0x0062E4E8, updatePrologue, updateBody, 9, 7, 0);

    // the cell check the ring searches make for every cell (16, timed), what it calls (17 counted, 18, 19, 22, 23 timed), the
    // per unit questions it asks about the units standing in the cell (20, 21, 24 to 26, 28, only counted) and the partition
    // query (27, counted)
    const BYTE cellCheckHead[10] = {0x55, 0x8B, 0xEC, 0x80, 0x3D, 0x85, 0x91, 0xDE, 0x00, 0x00};
    timeFunction(set, cave, 0x006F83EB, cellCheckHead, CELL_CHECK);
    const BYTE checkForAdjustHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C};
    countFunction(set, cave, 0x006F66D9, checkForAdjustHead, 17);
    const BYTE checkDestinationPrologue[10] = {0xB8, 0xAF, 0xD2, 0xB8, 0x00, 0xE8, 0x62, 0xB9, 0x34, 0x00};
    const BYTE checkDestinationBody[10] = {0x83, 0xEC, 0x58, 0x80, 0x3D, 0x85, 0x91, 0xDE, 0x00, 0x00};
    timeFramedFunction(set, cave, 0x006F1584, checkDestinationPrologue, checkDestinationBody, 18);
    const BYTE pathExistsHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28};
    timeFunction(set, cave, 0x006F5BB0, pathExistsHead, PATH_EXISTS);
    const BYTE occupancyHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};
    countFunction(set, cave, 0x006EB151, occupancyHead, OCCUPANCY);
    const BYTE relationshipHead[5] = {0x53, 0x8B, 0x5C, 0x24, 0x08};
    countFunction(set, cave, 0x0068D7AB, relationshipHead, RELATIONSHIP);
    const BYTE footprintHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28};
    timeFunction(set, cave, 0x006F0195, footprintHead, 22);
    const BYTE polygonHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C};
    timeFunction(set, cave, 0x006ECC06, polygonHead, 23);
    const BYTE unknownCheckHead[5] = {0x56, 0xFF, 0x74, 0x24, 0x0C};
    countFunction(set, cave, 0x0069519A, unknownCheckHead, 24);
    const BYTE alliedHead[6] = {0x8B, 0x89, 0xA4, 0x00, 0x00, 0x00};
    countFunction(set, cave, 0x0068B47F, alliedHead, 25);
    const BYTE currentObjectHead[9] = {0x8B, 0x41, 0x04, 0x56, 0xBE, 0x00, 0x20, 0x00, 0x00};
    countFunction(set, cave, 0x00693A1A, currentObjectHead, 26);
    const BYTE partitionHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C};
    countFunction(set, cave, 0x0046A575, partitionHead, 27);
    const BYTE hordeCheckHead[6] = {0x53, 0x56, 0x8B, 0x74, 0x24, 0x10};
    countFunction(set, cave, 0x006E8200, hordeCheckHead, 28);

    // the broad phase: sweep (29), contact callbacks (30) and pair clean up (31) timed, the pair table functions and the proxy
    // bound update counted (32 to 36)
    const BYTE sweepHead[5] = {0x83, 0xEC, 0x20, 0x53, 0x55};
    timeFunction(set, cave, 0x00B6D8A0, sweepHead, 29);
    const BYTE contactsHead[6] = {0x83, 0xEC, 0x0C, 0x53, 0x8B, 0xD9};
    timeFunction(set, cave, 0x00B6D060, contactsHead, 30);
    const BYTE cleanupHead[6] = {0x8B, 0x81, 0x10, 0xAE, 0x00, 0x00};
    timeFunction(set, cave, 0x00B6CF90, cleanupHead, 31);
    const BYTE pairIncrementHead[7] = {0x8B, 0x44, 0x24, 0x08, 0x83, 0xEC, 0x2C};
    countFunction(set, cave, 0x00B6D730, pairIncrementHead, 32);
    const BYTE pairDecrementHead[10] = {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x90, 0xA0, 0x00, 0x00, 0x00};
    countFunction(set, cave, 0x00B6D3B0, pairDecrementHead, 33);
    const BYTE tableInsertHead[10] = {0x53, 0x55, 0x8B, 0xE9, 0x8B, 0x9D, 0x4C, 0x12, 0x00, 0x00};
    countFunction(set, cave, 0x00B6D5E0, tableInsertHead, 34);
    const BYTE tableRemoveHead[5] = {0x8B, 0x44, 0x24, 0x04, 0x53};
    countFunction(set, cave, 0x00B6D2B0, tableRemoveHead, 35);
    const BYTE proxyBoundsHead[6] = {0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1};
    countFunction(set, cave, 0x00B6C7D0, proxyBoundsHead, 36);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
