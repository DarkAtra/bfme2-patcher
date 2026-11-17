#include <windows.h>
#include <cstdint>

#include "ai_tactic_teardown.h"
#include "patch_set.h"

// AI tactic teardown crash fix
//
// Problem: leaving a match with AI players sometimes crashes the game (online and in skirmish, about one exit in three in a test).
// Fix: the AI's tactics no longer read each other's freed memory while the AI is destroyed.
//
// When a player's tactical AI is destroyed (0x006C708B -> 0x006C6E7C), its tactic registry's destructor (0x0090BB5D, called at
// 0x006C6E89) deletes the tactics one after the other and never clears the entries. Each tactic's destructor (0x008F26C3) first
// ends the tactic (0x008F2006(this, 0, 1), called at 0x008F26E5), and ending it looks up the tactics of the AIs whose objects
// stand near it (0x008F1D88 -> 0x008F1A49 -> 0x008F157F -> 0x008F128B): through the same registry, whose earlier entries are
// already freed. Freed memory that has been reused (a crash dump showed a UI string in it) then crashes the walk. While the
// registry is destroyed, the dying tactics are no longer ended: the AI is going away with them, and nothing outside it reads what
// ending them would have computed. A tactic that ends while its AI lives (during a match) is ended as before.

using namespace patching;

namespace {

    constexpr uintptr_t REGISTRY_DESTROY_CALL = 0x006C6E89;
    constexpr uintptr_t REGISTRY_DESTROY = 0x0090BB5D;
    constexpr uintptr_t TACTIC_END_CALL = 0x008F26E5;
    constexpr uintptr_t TACTIC_END = 0x008F2006;

    bool registryDestroyed = false;

    void __fastcall destroyRegistry(void *registry) {
        registryDestroyed = true;
        reinterpret_cast<void (__thiscall *)(void *)>(REGISTRY_DESTROY)(registry);
        registryDestroyed = false;
    }

    // thiscall with two arguments, which this pops as the original does (ret 8)
    void __fastcall endTactic(void *tactic, void *, const int first, const int second) {
        if (registryDestroyed) {
            return;
        }
        reinterpret_cast<void (__thiscall *)(void *, int, int)>(TACTIC_END)(tactic, first, second);
    }
}

bool applyAiTacticTeardown() {
    static PatchSet set;
    set.reset();

    const BYTE registryDestroyCall[5] = {0xE8, 0xCF, 0x4C, 0x24, 0x00};
    set.redirectCall(REGISTRY_DESTROY_CALL, registryDestroyCall, reinterpret_cast<uintptr_t>(&destroyRegistry));
    // mov edi, [esi + 0x10] / test edi, edi / je / mov ecx, edi: the tactical AI's registry, destroyed when there is one
    const BYTE registry[9] = {0x8B, 0x7E, 0x10, 0x85, 0xFF, 0x74, 0x12, 0x8B, 0xCF};
    set.expect(0x006C6E80, registry);

    const BYTE tacticEndCall[5] = {0xE8, 0x1C, 0xF9, 0xFF, 0xFF};
    set.redirectCall(TACTIC_END_CALL, tacticEndCall, reinterpret_cast<uintptr_t>(&endTactic));
    // push 1 / push 0 / mov [ebp - 4], 2: the tactic destructor's end call, on ecx = this
    const BYTE endArguments[11] = {0x6A, 0x01, 0x6A, 0x00, 0xC7, 0x45, 0xFC, 0x02, 0x00, 0x00, 0x00};
    set.expect(0x008F26DA, endArguments);
    // mov dword [esi], AITactic's vtable (0x00C79664): the destructor's class
    const BYTE tacticVtable[6] = {0xC7, 0x06, 0x64, 0x96, 0xC7, 0x00};
    set.expect(0x008F26D4, tacticVtable);

    return commit(set, Cave(nullptr));
}
