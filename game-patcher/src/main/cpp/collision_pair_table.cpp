#include <windows.h>
#include <cstdint>
#include <cstring>

#include "collision_pair_table.h"
#include "patch_set.h"

// Collision pair table
//
// Problem: the collision broad phase tracks overlapping object pairs in a hash table of only 11131 buckets; with a big army
// clumped together each lookup walks ten nodes or more, and the sweep took about 12 ms of every logic frame.
// Fix: gives the table 262139 buckets in an extension of the broad phase object. Lookups find the same nodes, so the game
// behaves exactly as before; the sweep drops to about 4 ms.
//
// The game's collision broad phase (0x00B6E6B0) counts for every pair of proxies on how many axes their bounds overlap in a
// chained hash table of pair nodes: 11131 buckets (the hash is `(idLow * 0x10000 + idHigh) % 0x2B7B`) for 200 000 pairs and more
// when a big army is clumped, so the sweep (0x00B6D8A0) walks ten nodes or more per lookup, and a walk is a cache miss per node.
// That is most of the sweep's time (0x00B6D730 and 0x00B6D3B0 are the lookups).
//
// The table is only a lookup structure: nothing visits its pairs in bucket order (the pair callbacks run from the second table,
// which this patch leaves alone), and a node carries its own back link, so a node can sit in any table. The patch makes the
// game allocate the broad phase object 4 * BUCKETS bytes bigger (the only allocation site is the `push 0xC070` in 0x00B6C4C3),
// keeps a bigger table in that tail, and changes the two lookups, the table's clearing and its destruction to use it:
//  - 0x00B6D730 and 0x00B6D3B0 get the new modulus and hash and load the bucket from the tail instead of from +0x18;
//  - 0x00B6CC30 (moves every node into the free list and empties the buckets) is replaced;
//  - the constructor 0x00B6E220 clears the tail first, and the destructor 0x00B6D1B0 frees the nodes in the tail first.
// Lookups find the same pair node as before, so the game behaves exactly as without the patch.

using namespace patching;
using namespace collision_pair_table;

namespace {

    constexpr uintptr_t ALLOCATION_SIZE = 0x00B6C4C4;
    constexpr uintptr_t INCREMENT_HASH = 0x00B6D77C;
    constexpr uintptr_t INCREMENT_MODULUS = 0x00B6D785;
    constexpr uintptr_t INCREMENT_BUCKET = 0x00B6D793;
    constexpr uintptr_t INCREMENT_SLOT = 0x00B6D7F9;
    constexpr uintptr_t DECREMENT_HASH = 0x00B6D3F8;
    constexpr uintptr_t DECREMENT_MODULUS = 0x00B6D400;
    constexpr uintptr_t DECREMENT_BUCKET = 0x00B6D406;
    constexpr uintptr_t CLEAR = 0x00B6CC30;
    constexpr uintptr_t CONSTRUCTOR = 0x00B6E220;
    constexpr uintptr_t DESTRUCTOR = 0x00B6D1B0;

    constexpr uint32_t NEXT_IN_CHAIN = 0x30;
    constexpr uint32_t FREE_LIST = 0xADEC;

    // the game's operator delete
    constexpr uintptr_t OPERATOR_DELETE = 0x0042F6A0;
    // not const because the genuine code test (tools/tests/collision-pair-table) counts the releases
    uintptr_t deleteFunction = OPERATOR_DELETE;

    // Constructor: the game's memory is not cleared.
    void __cdecl prepare(uint8_t *broad) {
        memset(broad + TABLE, 0, sizeof(uint32_t) * BUCKETS);
    }

    // `tableBase` is the address of the original table (the object plus 0x18), as the game passes it: the free list head sits
    // 0xADEC bytes behind it.
    void __fastcall clear(uint8_t *tableBase) {
        auto *buckets = reinterpret_cast<uint32_t *>(tableBase - ORIGINAL_TABLE + TABLE);
        auto *freeList = reinterpret_cast<uint32_t *>(tableBase + FREE_LIST);
        for (uint32_t bucket = 0; bucket < BUCKETS; bucket++) {
            uint32_t node = buckets[bucket];
            while (node != 0) {
                auto *fields = reinterpret_cast<uint32_t *>(node);
                const uint32_t next = fields[NEXT_IN_CHAIN / 4];
                fields[NEXT_IN_CHAIN / 4] = *freeList;
                *freeList = node;
                node = next;
            }
            buckets[bucket] = 0;
        }
    }

    void __cdecl destroy(uint8_t *tableBase) {
        auto *buckets = reinterpret_cast<uint32_t *>(tableBase - ORIGINAL_TABLE + TABLE);
        const auto release = reinterpret_cast<void (__cdecl *)(void *)>(deleteFunction);
        for (uint32_t bucket = 0; bucket < BUCKETS; bucket++) {
            uint32_t node = buckets[bucket];
            while (node != 0) {
                const uint32_t next = reinterpret_cast<uint32_t *>(node)[NEXT_IN_CHAIN / 4];
                release(reinterpret_cast<void *>(node));
                node = next;
            }
            buckets[bucket] = 0;
        }
    }

    // push ecx / push ecx / call function / add esp, 4 / pop ecx / the replaced instructions / jmp back
    template<size_t N>
    uintptr_t emitBefore(Cave &cave, const uintptr_t function, const BYTE (&displaced)[N], const uintptr_t resume) {
        const uintptr_t entry = cave.here();
        cave.emit8(0x51);
        cave.emit8(0x51);
        cave.call(function);
        const BYTE restore[4] = {0x83, 0xC4, 0x04, 0x59};
        cave.emit(restore);
        cave.emit(displaced);
        cave.jmp(resume);
        return entry;
    }

    // imul eax, eax, MULTIPLIER / add eax, <the register that holds the high id>: the game's `shl eax, 16 / add eax, id`
    uintptr_t emitHash(Cave &cave, const BYTE (&add)[2], const uintptr_t resume) {
        const uintptr_t entry = cave.here();
        cave.emit8(0x69);
        cave.emit8(0xC0);
        cave.emit32(MULTIPLIER);
        cave.emit(add);
        cave.jmp(resume);
        return entry;
    }

    // The instruction with its displacement widened to the tail: `opcode modrm sib disp32`, then the instructions that follow it.
    template<size_t N>
    uintptr_t emitWidened(Cave &cave, const BYTE (&widened)[3], const BYTE (&rest)[N], const uintptr_t resume) {
        const uintptr_t entry = cave.here();
        cave.emit(widened);
        cave.emit32(TABLE);
        cave.emit(rest);
        cave.jmp(resume);
        return entry;
    }
}

bool applyCollisionPairTable() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();

    BYTE originalSize[4], newSize[4];
    putInt(originalSize, OBJECT_SIZE);
    putInt(newSize, static_cast<int>(OBJECT_SIZE + sizeof(uint32_t) * BUCKETS));
    set.replace(ALLOCATION_SIZE, originalSize, newSize);

    BYTE originalModulus[4], newModulus[4];
    putInt(originalModulus, ORIGINAL_BUCKETS);
    putInt(newModulus, BUCKETS);
    set.replace(INCREMENT_MODULUS, originalModulus, newModulus);
    set.replace(DECREMENT_MODULUS, originalModulus, newModulus);

    // shl eax, 16 / add eax, esi
    const BYTE incrementHash[5] = {0xC1, 0xE0, 0x10, 0x03, 0xC6};
    const BYTE incrementAdd[2] = {0x03, 0xC6};
    set.hook(INCREMENT_HASH, incrementHash, emitHash(cave, incrementAdd, INCREMENT_HASH + 5));

    // shl eax, 16 / add eax, ecx
    const BYTE decrementHash[5] = {0xC1, 0xE0, 0x10, 0x03, 0xC1};
    const BYTE decrementAdd[2] = {0x03, 0xC1};
    set.hook(DECREMENT_HASH, decrementHash, emitHash(cave, decrementAdd, DECREMENT_HASH + 5));

    // mov eax, [ebx + edx * 4 + 0x18] / test eax, eax
    const BYTE incrementBucket[6] = {0x8B, 0x44, 0x93, 0x18, 0x85, 0xC0};
    const BYTE incrementBucketRest[2] = {0x85, 0xC0};
    const BYTE incrementBucketWide[3] = {0x8B, 0x84, 0x93};
    set.hook(INCREMENT_BUCKET, incrementBucket, emitWidened(cave, incrementBucketWide, incrementBucketRest, INCREMENT_BUCKET + 6));

    // lea ecx, [ebx + edx * 4 + 0x18] / mov [eax + 0x2C], ecx
    const BYTE incrementSlot[7] = {0x8D, 0x4C, 0x93, 0x18, 0x89, 0x48, 0x2C};
    const BYTE incrementSlotRest[3] = {0x89, 0x48, 0x2C};
    const BYTE incrementSlotWide[3] = {0x8D, 0x8C, 0x93};
    set.hook(INCREMENT_SLOT, incrementSlot, emitWidened(cave, incrementSlotWide, incrementSlotRest, INCREMENT_SLOT + 7));

    // mov esi, [ebx + edx * 4 + 0x18] / test esi, esi
    const BYTE decrementBucket[6] = {0x8B, 0x74, 0x93, 0x18, 0x85, 0xF6};
    const BYTE decrementBucketRest[2] = {0x85, 0xF6};
    const BYTE decrementBucketWide[3] = {0x8B, 0xB4, 0x93};
    set.hook(DECREMENT_BUCKET, decrementBucket, emitWidened(cave, decrementBucketWide, decrementBucketRest, DECREMENT_BUCKET + 6));

    // push esi / push edi / xor esi, esi / mov eax, [ecx + esi * 4]: the whole function is replaced
    const BYTE clearHead[7] = {0x56, 0x57, 0x33, 0xF6, 0x8B, 0x04, 0xB1};
    set.hook(CLEAR, clearHead, reinterpret_cast<uintptr_t>(&clear));
    // the rest of it up to its ret, which clear reproduces: nodes onto the free list at + 0xADEC through + 0x30, 0x2B7B buckets
    const BYTE clearBody[52] = {
        0x85, 0xC0, 0x74, 0x1D, 0xEB, 0x03, 0x8D, 0x49, 0x00, 0x8B, 0x50, 0x30, 0x85, 0xD2, 0x8B, 0xB9, 0xEC, 0xAD, 0x00, 0x00,
        0x89, 0x78, 0x30, 0x89, 0x81, 0xEC, 0xAD, 0x00, 0x00, 0x8B, 0xC2, 0x75, 0xE8, 0xC7, 0x04, 0xB1, 0x00, 0x00, 0x00, 0x00,
        0x46, 0x81, 0xFE, 0x7B, 0x2B, 0x00, 0x00, 0x72, 0xCC, 0x5F, 0x5E, 0xC3
    };
    set.expectBytes(CLEAR + sizeof(clearHead), clearBody);
    // mov eax, [esp + 4] / test eax, eax / je: the operator delete destroy releases the tail's nodes with
    const BYTE deleteHead[8] = {0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x74, 0x0C};
    set.expect(OPERATOR_DELETE, deleteHead);

    // mov edx, ecx / push ebx / xor ebx, ebx / mov [edx], ebx
    const BYTE constructorHead[7] = {0x8B, 0xD1, 0x53, 0x33, 0xDB, 0x89, 0x1A};
    set.hook(CONSTRUCTOR, constructorHead, emitBefore(cave, reinterpret_cast<uintptr_t>(&prepare), constructorHead, CONSTRUCTOR + 7));

    // push ebx / push esi / push edi / mov ebx, ecx / xor edi, edi
    const BYTE destructorHead[7] = {0x53, 0x56, 0x57, 0x8B, 0xD9, 0x33, 0xFF};
    set.hook(DESTRUCTOR, destructorHead, emitBefore(cave, reinterpret_cast<uintptr_t>(&destroy), destructorHead, DESTRUCTOR + 7));

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
