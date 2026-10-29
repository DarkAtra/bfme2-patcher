#include <windows.h>
#include <cstdint>

#include "features.h"
#include "patch_set.h"
#include "health_bar_crash_fix.h"

// Health bar crash fix
//
// Problem: the stock game crashes when a big army is on screen (about 1500 health bars): the batch that draws the 2D overlays
// counts its indices in 16 bits, and beyond 65535 indices it copies the batch past the end of its index buffer.
// Fix: when a rectangle would take the batch past that limit, draws the batch collected so far and starts a new one, so every
// health bar stays visible (HEALTH_BAR_CRASH_FIX_FLUSH). With that switch off, the rectangles beyond the limit are not drawn.
//
// The game draws health bars, selection rectangles and other 2D overlays through one batch: every rectangle asks the batch
// (0x0051B720) for four vertices and six indices and fills them in, and one function (0x005199E0) copies the indices of the whole
// batch into a dynamic index buffer when the frame's overlays are flushed. The buffer is requested through a 16 bit count
// (0x005388E0 compares, and 0x00538D20 stores, `ushort` counts), so a batch of more than 65535 indices, which is 10922
// rectangles or about 1500 health bars, asks for a tiny slot in the buffer and then copies the whole batch into it: the copy runs
// past the end of the buffer and the game crashes (access violation in 0x0051A0B3, found in two crash dumps with a big army on
// screen). Both counters are only used for drawing, nothing of it reaches the game state.
//
// The allocation function is entered through a cave that, when the batch would pass the limit, draws the batch collected so far
// with the game's own flush (which also empties it) and then lets the allocation run on the empty batch, so every health bar
// stays visible. The game itself flushes this batch in the middle of a drawing loop (0x00924DD1 every 1000 line segments), so
// the batch is safe to flush between two rectangles. With HEALTH_BAR_CRASH_FIX_FLUSH off the cave hands out a scratch area
// instead: the rectangle is filled in there and never drawn, a health bar beyond the limit is missing, which beats a crash.

using namespace patching;

namespace {

    constexpr uintptr_t ALLOCATE = 0x0051B720;
    constexpr uintptr_t ALLOCATE_RESUME = 0x0051B728;
    // fastcall(batch): draws the batch and empties it
    constexpr uintptr_t FLUSH = 0x005199E0;
    constexpr uint32_t INDEX_LIMIT = 0xFFFF;
    // the largest request any of the five callers makes is four vertices (44 bytes each) and six indices
    constexpr uint32_t MAX_VERTICES = 32;
    constexpr uint32_t MAX_INDICES = 256;

    struct Scratch {
        BYTE vertices[MAX_VERTICES * 44];
        BYTE indices[MAX_INDICES * 2];
    };

    // The function is `thiscall(this, vertices, indices, int *indexOut, uint *baseOut)` and removes its 16 bytes of arguments; it
    // returns the vertex pointer and stores the pointer to the indices and the number of the first vertex in its out arguments.
    //
    //   mov eax, [ecx + 0x2C]          the indices in the batch
    //   add eax, [esp + 8]
    //   cmp eax, 0xFFFF
    //   ja full
    //   the replaced instructions / jmp resume
    // full:
    //   cmp dword [esp + 4], MAX_VERTICES / ja resume path / cmp dword [esp + 8], MAX_INDICES / ja resume path
    // flushing:
    //   push ecx / call FLUSH / pop ecx / jmp to the replaced instructions
    // dropping:
    //   mov eax, [esp + 0xC] / mov dword [eax], scratch indices / mov eax, [esp + 0x10] / mov dword [eax], 0
    //   mov eax, scratch vertices / ret 0x10
    enum class Mode { Drop, Flush };

    uintptr_t emitHook(Cave &cave, const Scratch *scratch, const uintptr_t resume, const BYTE (&displaced)[8], const Mode mode, const uintptr_t flush) {
        const uintptr_t entry = cave.here();
        const BYTE count[3] = {0x8B, 0x41, 0x2C};
        cave.emit(count);
        const BYTE add[4] = {0x03, 0x44, 0x24, 0x08};
        cave.emit(add);
        cave.emit8(0x3D);
        cave.emit32(INDEX_LIMIT);
        const size_t jumpToFull = cave.size;
        cave.jcc(JA, 0); // full

        // the replaced instructions must run with eax free again: they set up the frame and use eax themselves
        const uintptr_t original = cave.here();
        cave.emit(displaced);
        cave.jmp(resume);

        const uintptr_t full = cave.here();
        cave.resolveJcc(jumpToFull, full);
        const BYTE tooManyVertices[5] = {0x83, 0x7C, 0x24, 0x04, static_cast<BYTE>(MAX_VERTICES)};
        cave.emit(tooManyVertices);
        const size_t jumpOne = cave.size;
        cave.jcc(JA, 0);
        const BYTE tooManyIndices[4] = {0x81, 0x7C, 0x24, 0x08}; // cmp dword [esp + 8], MAX_INDICES
        cave.emit(tooManyIndices);
        cave.emit32(MAX_INDICES);
        const size_t jumpTwo = cave.size;
        cave.jcc(JA, 0);
        cave.resolveJcc(jumpOne, original);
        cave.resolveJcc(jumpTwo, original);

        if (mode == Mode::Flush) {
            cave.emit8(0x51); // push ecx
            cave.call(flush);
            cave.emit8(0x59); // pop ecx
            cave.jmp(original);
            return entry;
        }

        const BYTE indexOut[4] = {0x8B, 0x44, 0x24, 0x0C}; // mov eax, [esp + 0xC]
        cave.emit(indexOut);
        const BYTE storeIndices[2] = {0xC7, 0x00}; // mov dword [eax], scratch indices
        cave.emit(storeIndices);
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scratch->indices)));
        const BYTE baseOut[4] = {0x8B, 0x44, 0x24, 0x10}; // mov eax, [esp + 0x10]
        cave.emit(baseOut);
        const BYTE storeBase[6] = {0xC7, 0x00, 0x00, 0x00, 0x00, 0x00}; // mov dword [eax], 0
        cave.emit(storeBase);
        cave.emit8(0xB8); // mov eax, scratch vertices
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scratch->vertices)));
        const BYTE ret[3] = {0xC2, 0x10, 0x00};
        cave.emit(ret);
        return entry;
    }
}

bool applyHealthBarCrashFix() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    // the scratch area is behind the code in the same allocation
    static_assert(sizeof(Scratch) < CAVE_SIZE / 2, "the scratch area must leave room for the code");
    auto *scratch = reinterpret_cast<Scratch *>(caveMemory + CAVE_SIZE / 2);

    Cave cave(caveMemory);
    // push -1 / mov eax, fs:[0]
    const BYTE head[8] = {0x6A, 0xFF, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00};
    const Mode mode = features::HEALTH_BAR_CRASH_FIX_FLUSH ? Mode::Flush : Mode::Drop;
    const uintptr_t entry = emitHook(cave, scratch, ALLOCATE_RESUME, head, mode, FLUSH);

    static PatchSet set;
    set.reset();
    set.hook(ALLOCATE, head, entry);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
