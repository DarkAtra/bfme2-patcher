#include <windows.h>
#include <intrin.h>
#include <cstdint>

#include "features.h"
#include "patch_set.h"
#include "pathfinder_timing.h"
#include "stats.h"

// For the statistics builds only: times AdjustDestination (0x006FE456), which the game calls from 32 places. The function
// starts with push ebp / mov ebp, esp / sub esp, 0x9C, which a cave can run itself, so the cave notes the time, swaps the
// return address for one of its own and takes the time again when the function comes back.

using namespace patching;

namespace {

    constexpr uintptr_t ADJUST_DESTINATION = 0x006FE456;
    constexpr uintptr_t ADJUST_DESTINATION_BODY = 0x006FE45F;
    constexpr size_t MAX_DEPTH = 16;

    struct Entry {
        uint32_t returnAddress;
        uint64_t start;
    };

    Entry entries[MAX_DEPTH];
    size_t depth = 0;
    uintptr_t exitAddress = 0;

    void __cdecl entered(uint32_t *returnSlot) {
        Entry &entry = entries[depth % MAX_DEPTH];
        entry.returnAddress = *returnSlot;
        entry.start = __rdtsc();
        depth++;
        *returnSlot = static_cast<uint32_t>(exitAddress);
    }

    uint32_t __cdecl left() {
        const uint64_t end = __rdtsc();
        depth--;
        const Entry &entry = entries[depth % MAX_DEPTH];
        Stats *statistics = stats();
        statistics->adjustCalls++;
        uint64_t cycles = (static_cast<uint64_t>(statistics->adjustCyclesHigh) << 32) | statistics->adjustCyclesLow;
        cycles += end - entry.start;
        statistics->adjustCyclesLow = static_cast<uint32_t>(cycles);
        statistics->adjustCyclesHigh = static_cast<uint32_t>(cycles >> 32);
        return entry.returnAddress;
    }
}

bool applyPathfinderTiming() {

    if (!features::STATS || stats() == nullptr) {
        return true;
    }

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    // push ecx (this) / lea eax, [esp + 4] (the return address) / push eax / call entered / add esp, 4 / pop ecx
    const uintptr_t entry = cave.here();
    const BYTE save[5] = {0x51, 0x8D, 0x44, 0x24, 0x04};
    cave.emit(save);
    cave.emit8(0x50);
    cave.call(reinterpret_cast<uintptr_t>(&entered));
    const BYTE restore[4] = {0x83, 0xC4, 0x04, 0x59};
    cave.emit(restore);
    // the instructions the hook replaced
    const BYTE displaced[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x9C, 0x00, 0x00, 0x00};
    cave.emit(displaced);
    cave.jmp(ADJUST_DESTINATION_BODY);

    // push eax (the result) / call left / mov ecx, eax / pop eax / jmp ecx
    exitAddress = cave.here();
    cave.emit8(0x50);
    cave.call(reinterpret_cast<uintptr_t>(&left));
    const BYTE finish[5] = {0x8B, 0xC8, 0x58, 0xFF, 0xE1};
    cave.emit(finish);

    static PatchSet set;
    set.reset();

    const BYTE head[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x9C, 0x00, 0x00, 0x00};
    set.hook(ADJUST_DESTINATION, head, entry);
    // cmp byte [0x00DE9185], 0
    const BYTE body[7] = {0x80, 0x3D, 0x85, 0x91, 0xDE, 0x00, 0x00};
    set.expect(ADJUST_DESTINATION_BODY, body);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
