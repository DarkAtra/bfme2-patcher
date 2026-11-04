#include <windows.h>
#include <cstdint>
#include <cstring>

#include "material_parameters.h"
#include "patch_set.h"

// Material parameters
//
// Problem: special effects made of W3D models (particle systems with Type = DRAWABLE, e.g. the Balrog summon or Cloudbreak) cost
// up to several milliseconds a frame. Every frame RenderObjectDrawModule's draw (0x00964C00) writes each visible particle's colour or
// opacity into its model through 0x0050E040 (ColorEmissive), 0x0050E244 (Opacity) or 0x0050E413 (ColorDiffuse). For a shader material
// each call copies the material's whole parameter list, replaces the one entry and re-records the material's D3DX parameter block
// (0x00551F8F: every parameter looked up by name, textures by name), about 21 us a call. Particles step 30 times a second and are
// drawn 60 times, so at least every other call writes the value the material already holds. About ten other drawing call sites use the
// same setters.
// Fix: a setter skips the rebuild when the found entry already holds exactly what the rebuild would write into it (0x00550C7A copies
// the name, the type, the string, the 16 value bytes, +0x1C and +0x20 of an entry cleared by 0x00550B8C) and the material has a
// recorded block (+0x24). The rebuild resets the list to the shader's defaults and merges the list it was given back in, so with the
// same entry it records the same list again. Everything else the setter does (the render lock, the vertex material loop) is unchanged.
//
// The material's list (+0x0C) and block (+0x24) only change together, in 0x00551F8F and in 0x005521C0, which deletes the block first.
// Like the stock game for every material whose parameters are set once, this relies on the block staying valid while the material
// holds its effect.
//
// Each setter has the same 14 bytes after its name lookup (eax the entry or null, esi the material, edi the name, the new values
// at [ebp + 0x0C]): test eax, eax / jz no / cmp [eax + 4], type / jnz no / mov bl, 1 / jmp on / no: xor bl, bl / on: ...
// The hook asks unchangedParameter() and goes to `no` (bl = 0, no rebuild) when it answers yes.

using namespace patching;

namespace {

    struct Setter {
        uintptr_t start;
        BYTE type;
        BYTE count;
        uintptr_t name;
        // `mov ecx, esi / call 0x00551F8F`, from the start
        uintptr_t rebuildCall;
    };

    constexpr Setter SETTERS[] = {
        {0x0050E040, 4, 3, 0x00BE5A04, 0x115},
        {0x0050E244, 2, 1, 0x00BDD2D0, 0x101},
        {0x0050E413, 4, 3, 0x00BE5A14, 0x115},
    };

    // the decision window, from the setter's start
    constexpr uintptr_t WINDOW = 0xA6;

    // an AsciiString is one pointer to a block whose characters start at +8, null for the empty string
    const char *characters(const uint8_t *string) {
        const auto block = *reinterpret_cast<const uint8_t *const *>(string);
        return block == nullptr ? "" : reinterpret_cast<const char *>(block + 8);
    }

    bool __stdcall unchangedParameter(const uint8_t *entry, const uint8_t *material, const char *name, const uint32_t type,
                                      const uint32_t *values, const uint32_t count) {
        if (*reinterpret_cast<const uint32_t *>(material + 0x24) == 0) {
            return false;
        }
        if (strcmp(characters(entry), name) != 0 || *reinterpret_cast<const uint32_t *>(entry + 4) != type ||
            characters(entry + 8)[0] != 0) {
            return false;
        }
        const auto stored = reinterpret_cast<const uint32_t *>(entry + 0x0C);
        for (uint32_t i = 0; i < 4; i++) {
            if (stored[i] != (i < count ? values[i] : 0)) {
                return false;
            }
        }
        return *reinterpret_cast<const uint32_t *>(entry + 0x1C) == 0 && entry[0x20] == 0;
    }

    void addSetter(PatchSet &set, Cave &cave, const Setter &setter) {
        const uintptr_t at = setter.start + WINDOW;
        const uintptr_t no = at + 14;
        const uintptr_t on = at + 16;

        // mov edi, name: the name the lookup used and the cave passes on
        BYTE name[5] = {0xBF};
        memcpy(name + 1, &setter.name, sizeof(uint32_t));
        set.expect(at - 0x23, name);
        // lea ecx, [esi + 0x0C] / mov dword [ebp - 0x10], 1 / call 0x0050DFFF: the lookup in the material's list
        const BYTE lookup[10] = {0x8D, 0x4E, 0x0C, 0xC7, 0x45, 0xF0, 0x01, 0x00, 0x00, 0x00};
        set.expect(at - 0x0F, lookup);
        BYTE lookupCall[5] = {0xE8};
        const auto lookupDisplacement = static_cast<int32_t>(0x0050DFFF - at);
        memcpy(lookupCall + 1, &lookupDisplacement, sizeof(lookupDisplacement));
        set.expect(at - 5, lookupCall);
        // test byte [ebp - 0x10], 1 / jz +8 / lea ecx, [ebp - 0x14] / call release / test bl, bl / jz: bl decides the rebuild
        const BYTE decision[4] = {0xF6, 0x45, 0xF0, 0x01};
        set.expect(on, decision);
        const BYTE testFlag[3] = {0x84, 0xDB, 0x74};
        set.expect(on + 14, testFlag);
        // mov ecx, esi / call 0x00551F8F: the rebuild of this material
        BYTE rebuild[7] = {0x8B, 0xCE, 0xE8};
        const auto rebuildDisplacement = static_cast<int32_t>(0x00551F8F - (setter.start + setter.rebuildCall + 7));
        memcpy(rebuild + 3, &rebuildDisplacement, sizeof(rebuildDisplacement));
        set.expect(setter.start + setter.rebuildCall, rebuild);

        const BYTE stock[14] = {0x85, 0xC0, 0x74, 0x0A, 0x83, 0x78, 0x04, setter.type, 0x75, 0x04, 0xB3, 0x01, 0xEB, 0x02};
        set.hook(at, stock, cave.here());

        const BYTE found[2] = {0x85, 0xC0}; // test eax, eax
        cave.emit(found);
        cave.jcc(JE, no);
        const BYTE typed[4] = {0x83, 0x78, 0x04, setter.type}; // cmp dword [eax + 4], type
        cave.emit(typed);
        cave.jcc(JNE, no);
        const BYTE save[2] = {0x51, 0x52}; // push ecx / push edx
        cave.emit(save);
        cave.emit8(0x6A); // push count
        cave.emit8(setter.count);
        const BYTE values[4] = {0x8D, 0x4D, 0x0C, 0x51}; // lea ecx, [ebp + 0x0C] / push ecx
        cave.emit(values);
        cave.emit8(0x6A); // push type
        cave.emit8(setter.type);
        cave.emit8(0x68); // push name
        cave.emit32(static_cast<uint32_t>(setter.name));
        const BYTE materialAndEntry[2] = {0x56, 0x50}; // push esi / push eax
        cave.emit(materialAndEntry);
        cave.call(reinterpret_cast<uintptr_t>(&unchangedParameter));
        const BYTE restore[4] = {0x5A, 0x59, 0x84, 0xC0}; // pop edx / pop ecx / test al, al
        cave.emit(restore);
        cave.jcc(JNE, no);
        const BYTE rebuildIt[2] = {0xB3, 0x01}; // mov bl, 1
        cave.emit(rebuildIt);
        cave.jmp(on);
    }
}

bool applyMaterialParameters() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);
    static PatchSet set;
    set.reset();
    for (const Setter &setter : SETTERS) {
        addSetter(set, cave, setter);
    }

    // the rebuild: esi = the material, its block at +0x24 (cmp [esi + 0x24], ebx / jz / ... / push [esi + 0x24] / ... DeleteParameterBlock)
    const BYTE rebuildHead[10] = {0x53, 0x56, 0x8B, 0xF1, 0x57, 0x8D, 0x7E, 0x08, 0x33, 0xDB};
    set.expect(0x00551F9C, rebuildHead);
    const BYTE rebuildBlock[10] = {0x39, 0x5E, 0x24, 0x74, 0x16, 0x8B, 0xCF, 0xE8, 0xFF, 0xED};
    set.expect(0x00551FC6, rebuildBlock);
    // the entry layout: 0x00550B8C clears the name (+0), the type (+4), the string (+8), 16 bytes at +0x0C, 4 at +0x1C and 1 at +0x20
    const BYTE clear[64] = {
        0x56, 0x8B, 0xF1, 0xE8, 0xBC, 0x51, 0xEE, 0xFF, 0x83, 0x66, 0x04, 0x00, 0x8D, 0x4E, 0x08, 0xE8, 0xB0, 0x51, 0xEE, 0xFF, 0x6A, 0x10,
        0x8D, 0x46, 0x0C, 0x6A, 0x00, 0x50, 0xE8, 0x7B, 0xC3, 0x4E, 0x00, 0x6A, 0x04, 0x8D, 0x46, 0x1C, 0x6A, 0x00, 0x50, 0xE8, 0x6E, 0xC3,
        0x4E, 0x00, 0x6A, 0x01, 0x6A, 0x00, 0x83, 0xC6, 0x20, 0x56, 0xE8, 0x61, 0xC3, 0x4E, 0x00, 0x83, 0xC4, 0x24, 0x5E, 0xC3};
    set.expectBytes(0x00550B8C, clear);
    // 0x00550C7A copies the same fields into the stored entry; an AsciiString's characters start at +8 of its block
    const BYTE copy[119] = {
        0x53, 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x8B, 0x06, 0x85, 0xC0, 0x57, 0x8B, 0xF9, 0xBB, 0x3F, 0x0C, 0xBD, 0x00, 0x74, 0x05, 0x83, 0xC0,
        0x08, 0xEB, 0x02, 0x8B, 0xC3, 0x50, 0x8B, 0xCF, 0xE8, 0x49, 0x44, 0xEB, 0xFF, 0x8B, 0x46, 0x04, 0x89, 0x47, 0x04, 0x8B, 0x46, 0x08,
        0x85, 0xC0, 0x74, 0x05, 0x83, 0xC0, 0x08, 0xEB, 0x02, 0x8B, 0xC3, 0x50, 0x8B, 0xCF, 0xE8, 0x13, 0xFF, 0xFF, 0xFF, 0x6A, 0x10, 0x8D,
        0x46, 0x0C, 0x50, 0x8D, 0x47, 0x0C, 0x50, 0xE8, 0x5A, 0xC2, 0x4E, 0x00, 0x6A, 0x04, 0x8D, 0x46, 0x1C, 0x50, 0x8D, 0x47, 0x1C, 0x50,
        0xE8, 0x4B, 0xC2, 0x4E, 0x00, 0x6A, 0x01, 0x83, 0xC6, 0x20, 0x8D, 0x47, 0x20, 0x56, 0x50, 0xE8, 0x3C, 0xC2, 0x4E, 0x00, 0x83, 0xC4,
        0x24, 0x8B, 0xC7, 0x5F, 0x5E, 0x5B, 0xC2, 0x04, 0x00};
    set.expectBytes(0x00550C7A, copy);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
