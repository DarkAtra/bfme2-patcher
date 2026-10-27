#include <windows.h>
#include <cstdint>

#include "contact_scan.h"
#include "patch_set.h"

// The collision broad phase's contact pass (0x00B6D060) calls the contact handler of an object's collider (0x0068EEC3) twice for
// every colliding pair in every update. The handler walks the object's module array (about 37 modules) and asks every module for
// its collide interface: a virtual call through the vtable at module + 0xC, slot 4. For all but about three modules per object
// that call lands in the game's default getter (0x00851E97, `xor eax, eax / ret`), so most of the pass is spent calling a function
// that always returns null, a cache miss on the module and an indirect call per module.
//
// This replaces the handler with the same code, written so that a module whose getter is that default is skipped without calling
// it. The result is the same: the default returns null, which the handler skips as well, and every other getter is still called, in
// the same order, with the same arguments, as are the status test and the modules' own contact functions.

using namespace patching;

namespace {

    constexpr uintptr_t HANDLER = 0x0068EEC3;
    constexpr uintptr_t DEFAULT_GETTER = 0x00851E97;
    // thiscall(object, bit): whether the bit is set in the object's status bits (Object + 0x94)
    constexpr uintptr_t STATUS_TEST = 0x0044DDEC;

    constexpr uint32_t OBJECT_OF_COLLIDER = 0x70;

    typedef void *(__fastcall *Getter)(void *module);
    typedef bool (__fastcall *StatusTest)(void *object, void *, uint32_t bit);
    typedef void (__fastcall *Collide)(void *collide, void *, void *other, void *second, void *third);

    uint32_t word(const uint8_t *base, const uint32_t offset) {
        return *reinterpret_cast<const uint32_t *>(base + offset);
    }

    // thiscall(collider, other, second, third), ret 0xC
    void __fastcall contactHandler(uint8_t *collider, void *, uint8_t *other, void *second, void *third) {

        if (word(collider, 0x434) == word(other, 0x74)) {
            return;
        }
        if (word(other, 0x4A4) != 0 && word(collider, 4) != 0) {
            return;
        }

        const auto *modules = reinterpret_cast<const uint32_t *>(word(collider, 0x1DC));
        for (; *modules != 0; modules++) {
            const auto *module = reinterpret_cast<const uint8_t *>(*modules);
            const uint32_t getter = word(reinterpret_cast<const uint8_t *>(word(module, 0xC)), 4);
            if (getter == DEFAULT_GETTER) {
                continue;
            }
            void *collide = reinterpret_cast<Getter>(getter)(const_cast<uint8_t *>(module) + 0xC);
            if (collide == nullptr) {
                continue;
            }
            if (reinterpret_cast<StatusTest>(STATUS_TEST)(collider - OBJECT_OF_COLLIDER, nullptr, 4)) {
                return;
            }
            const uint32_t onCollide = word(reinterpret_cast<const uint8_t *>(word(reinterpret_cast<const uint8_t *>(collide), 0)), 0);
            reinterpret_cast<Collide>(onCollide)(collide, nullptr, other, second, third);
        }
    }
}

bool applyContactScan() {

    static PatchSet set;
    set.reset();

    // mov eax, [esp + 4] / push ebx / mov ebx, ecx: the whole function is replaced
    const BYTE head[7] = {0x8B, 0x44, 0x24, 0x04, 0x53, 0x8B, 0xD9};
    set.hook(HANDLER, head, reinterpret_cast<uintptr_t>(&contactHandler));

    // xor eax, eax / ret: the getter the handler's modules default to
    const BYTE defaultGetter[3] = {0x33, 0xC0, 0xC3};
    set.expect(DEFAULT_GETTER, defaultGetter);

    // the status test: mov edx, [esp + 4] / xor eax, eax / push esi / mov esi, ecx
    const BYTE statusTest[7] = {0x8B, 0x54, 0x24, 0x04, 0x33, 0xC0, 0x56};
    set.expect(STATUS_TEST, statusTest);

    return commit(set, Cave(nullptr));
}
