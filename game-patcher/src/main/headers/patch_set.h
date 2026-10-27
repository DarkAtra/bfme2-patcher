#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>

// Shared machinery of the in-memory patches. A patch collects every byte range it reads or rewrites in a PatchSet, and
// commit() verifies all of them before writing any, so that a patch is applied as a whole or not at all.

namespace patching {

    constexpr size_t MAX_PATCH_LENGTH = 24;
    constexpr size_t MAX_PATCHES = 48;
    constexpr size_t CAVE_SIZE = 0x1000;

    struct Patch {
        uintptr_t address;
        size_t length;
        BYTE expected[MAX_PATCH_LENGTH];
        BYTE replacement[MAX_PATCH_LENGTH];
        bool write;
    };

    struct PatchSet {
        Patch patches[MAX_PATCHES];
        size_t count = 0;
        bool overflow = false;

        void reset() {
            count = 0;
            overflow = false;
        }

        template<size_t N>
        void expect(const uintptr_t address, const BYTE (&expected)[N]) {
            static_assert(N <= MAX_PATCH_LENGTH, "patch too long");
            Patch *patch = next();
            if (patch == nullptr) {
                return;
            }
            patch->address = address;
            patch->length = N;
            memcpy(patch->expected, expected, N);
            patch->write = false;
        }

        template<size_t N>
        void replace(const uintptr_t address, const BYTE (&expected)[N], const BYTE (&replacement)[N]) {
            static_assert(N <= MAX_PATCH_LENGTH, "patch too long");
            Patch *patch = next();
            if (patch == nullptr) {
                return;
            }
            patch->address = address;
            patch->length = N;
            memcpy(patch->expected, expected, N);
            memcpy(patch->replacement, replacement, N);
            patch->write = true;
        }

        // Replaces the window with a jump to `target` and pads the rest of it with nops.
        template<size_t N>
        void hook(const uintptr_t address, const BYTE (&expected)[N], const uintptr_t target) {
            static_assert(N >= 5, "a jump needs five bytes");
            BYTE replacement[N];
            memset(replacement, 0x90, N);
            replacement[0] = 0xE9;
            const auto displacement = static_cast<int32_t>(target - (address + 5));
            memcpy(replacement + 1, &displacement, sizeof(displacement));
            replace(address, expected, replacement);
        }

        void redirectCall(const uintptr_t address, const BYTE (&expected)[5], const uintptr_t target) {
            BYTE replacement[5];
            replacement[0] = 0xE8;
            const auto displacement = static_cast<int32_t>(target - (address + 5));
            memcpy(replacement + 1, &displacement, sizeof(displacement));
            replace(address, expected, replacement);
        }

    private:
        Patch *next() {
            if (count == MAX_PATCHES) {
                overflow = true;
                return nullptr;
            }
            return &patches[count++];
        }
    };

    // A small executable allocation the patches jump into. Everything is absolute: the engine is not relocated and the
    // cave is never moved.
    struct Cave {
        BYTE *base;
        size_t size = 0;

        explicit Cave(BYTE *base) : base(base) {}

        uintptr_t here() const {
            return reinterpret_cast<uintptr_t>(base) + size;
        }

        template<size_t N>
        void emit(const BYTE (&bytes)[N]) {
            memcpy(base + size, bytes, N);
            size += N;
        }

        void emit32(const uint32_t value) {
            memcpy(base + size, &value, sizeof(value));
            size += sizeof(value);
        }

        void emit8(const BYTE value) {
            base[size++] = value;
        }

        void jmp(const uintptr_t target) {
            emit8(0xE9);
            emitRelative(target);
        }

        void call(const uintptr_t target) {
            emit8(0xE8);
            emitRelative(target);
        }

        // `call dword [address]`, through an import slot
        void callThrough(const uintptr_t address) {
            emit8(0xFF);
            emit8(0x15);
            emit32(static_cast<uint32_t>(address));
        }

        // `jmp dword [address]`
        void jmpThrough(const uintptr_t address) {
            emit8(0xFF);
            emit8(0x25);
            emit32(static_cast<uint32_t>(address));
        }

        void jcc(const BYTE condition, const uintptr_t target) {
            emit8(0x0F);
            emit8(condition);
            emitRelative(target);
        }

        // Points the rel32 of a jcc emitted earlier at `target`. Emit the jcc with any target, e.g. 0, and keep `size`
        // from before it as `jccOffset`.
        void resolveJcc(const size_t jccOffset, const uintptr_t target) {
            const auto displacement = static_cast<uint32_t>(target - (reinterpret_cast<uintptr_t>(base) + jccOffset + 6));
            memcpy(base + jccOffset + 2, &displacement, sizeof(displacement));
        }

        void resolveJmp(const size_t jmpOffset, const uintptr_t target) {
            const auto displacement = static_cast<uint32_t>(target - (reinterpret_cast<uintptr_t>(base) + jmpOffset + 5));
            memcpy(base + jmpOffset + 1, &displacement, sizeof(displacement));
        }

        // `inc dword [address]`
        void increment(const uintptr_t address) {
            emit8(0xFF);
            emit8(0x05);
            emit32(static_cast<uint32_t>(address));
        }

    private:
        void emitRelative(const uintptr_t target) {
            emit32(static_cast<uint32_t>(target - (here() + 4)));
        }
    };

    // the second opcode byte of the two byte `0F 8x` conditional jumps
    constexpr BYTE JO = 0x80;
    constexpr BYTE JB = 0x82;
    constexpr BYTE JAE = 0x83;
    constexpr BYTE JE = 0x84;
    constexpr BYTE JNE = 0x85;

    inline BYTE *allocateCave() {
        return static_cast<BYTE *>(VirtualAlloc(nullptr, CAVE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    }

    inline void putInt(BYTE *destination, const int value) {
        memcpy(destination, &value, sizeof(value));
    }

    // The compiler folded the engine's constants in double and narrowed them to float, which reproduces the shipped bytes.
    inline void putFloat(BYTE *destination, const double value) {
        const auto narrowed = static_cast<float>(value);
        memcpy(destination, &narrowed, sizeof(narrowed));
    }

    inline bool isReadable(const uintptr_t address, const size_t length) {
        MEMORY_BASIC_INFORMATION memoryInfo;
        if (!VirtualQuery(reinterpret_cast<const void *>(address), &memoryInfo, sizeof(memoryInfo))) {
            return false;
        }
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(memoryInfo.BaseAddress) + memoryInfo.RegionSize;
        return memoryInfo.State == MEM_COMMIT
            && (memoryInfo.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0
            && address + length <= regionEnd;
    }

    inline bool verify(const PatchSet &set) {
        for (size_t i = 0; i < set.count; i++) {
            const Patch &patch = set.patches[i];
            if (!isReadable(patch.address, patch.length)) {
                return false;
            }
            if (memcmp(reinterpret_cast<const void *>(patch.address), patch.expected, patch.length) != 0) {
                return false;
            }
        }
        return true;
    }

    inline bool writeBytes(const uintptr_t address, const BYTE *bytes, const size_t length) {
        DWORD previousProtection;
        if (!VirtualProtect(reinterpret_cast<void *>(address), length, PAGE_EXECUTE_READWRITE, &previousProtection)) {
            return false;
        }
        memcpy(reinterpret_cast<void *>(address), bytes, length);
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void *>(address), length, previousProtection, &ignored);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(address), length);
        return true;
    }

    // Writes every patch, and puts the original bytes back if one of the writes fails.
    inline bool write(const PatchSet &set) {
        for (size_t i = 0; i < set.count; i++) {
            const Patch &patch = set.patches[i];
            if (!patch.write) {
                continue;
            }
            if (!writeBytes(patch.address, patch.replacement, patch.length)) {
                for (size_t j = 0; j < i; j++) {
                    if (set.patches[j].write) {
                        writeBytes(set.patches[j].address, set.patches[j].expected, set.patches[j].length);
                    }
                }
                return false;
            }
        }
        return true;
    }

    inline bool commit(const PatchSet &set, const Cave &cave) {
        return !set.overflow && cave.size <= CAVE_SIZE && verify(set) && write(set);
    }
}
