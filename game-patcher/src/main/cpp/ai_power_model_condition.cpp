#include <windows.h>
#include <cstdint>

#include "ai_power_model_condition.h"
#include "patch_set.h"

// A command button can be disabled by the model conditions of its object (EnableOnModelCondition, DisableOnModelCondition). The
// control bar asks 0x00942490 for that before a player may press it. The AI casts its heroes' and buildings' powers through
// AISpecialPowerUpdate, whose update (0x008B73B7) checks only that the power is ready and never asks about the button, so the AI
// casts powers the player cannot: patch 2.02 disables every hero ability while the hero is silenced (USER_67) and the weather
// spells while another weather spell is active (USER_69), and the AI ignored both.
//
// The hook sits right after the ready check, before the AI rolls its chance to cast. When the button is disabled for the object
// the update leaves by its own exit for a power that is not ready, so it tries again at its next wake-up. Only AISpecialPowerUpdate
// runs this code; player presses and DoCommandUpgrade do not. Derived from pySAGE's ai-power-model-condition
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t UPDATE = 0x008B73B7;
    constexpr uintptr_t UPDATE_VTABLE_SLOT = 0x00C6D948;
    constexpr uintptr_t BUTTON_AND_READY = 0x008B7431;
    constexpr uintptr_t AFTER_READY = 0x008B7457;
    constexpr uintptr_t CAST_STEP = 0x008B745D;
    constexpr uintptr_t NOT_CAST = 0x008B7493;
    // stdcall(button, object): 2 when the button is available for the object, 3 when its model conditions disable it
    constexpr uintptr_t BUTTON_STATE = 0x00942490;

    constexpr BYTE DISABLED = 3;
}

bool applyAiPowerModelCondition() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // esi is the module's update interface: [esi - 8] the object, [esi + 0x14] the power's behaviour with the bound button at +0xC
    // (not null here). edi is the module data. The button state keeps ebx, esi and edi.
    Cave cave(caveMemory);
    const BYTE pushArguments[9] = {
        0xFF, 0x76, 0xF8, // push dword [esi - 8]
        0x8B, 0x46, 0x14, // mov eax, [esi + 0x14]
        0xFF, 0x70, 0x0C, // push dword [eax + 0xC]
    };
    cave.emit(pushArguments);
    cave.call(BUTTON_STATE);
    const BYTE isDisabled[3] = {0x83, 0xF8, DISABLED}; // cmp eax, 3
    cave.emit(isDisabled);
    cave.jcc(JE, NOT_CAST);
    const BYTE displaced[6] = {0x8A, 0x4F, 0x19, 0x8B, 0x46, 0xF8}; // mov cl, [edi + 0x19] / mov eax, [esi - 8]
    cave.emit(displaced);
    cave.jmp(CAST_STEP);

    static PatchSet set;
    set.reset();

    // push ebx / push esi / mov esi, ecx / xor ebx, ebx
    const BYTE updateHead[6] = {0x53, 0x56, 0x8B, 0xF1, 0x33, 0xDB};
    set.expect(UPDATE, updateHead);

    // the update is slot 0 of AISpecialPowerUpdate's update interface
    const BYTE updateSlot[4] = {0xB7, 0x73, 0x8B, 0x00};
    set.expect(UPDATE_VTABLE_SLOT, updateSlot);

    // the bound button, its null test and the power's ready check
    const BYTE buttonTest[24] = {
        0x8B, 0x46, 0x14, 0x8B, 0x40, 0x0C, 0x3B, 0xC3, 0x74, 0x63, 0x8B, 0x40, 0x44, 0x3B, 0xC3, 0x57, 0x8B, 0x7E, 0xF4, 0x74, 0x0B,
        0x50, 0x8D, 0x4E,
    };
    set.expect(BUTTON_AND_READY, buttonTest);
    const BYTE readyTest[14] = {0xF0, 0xE8, 0x3C, 0xFC, 0xFF, 0xFF, 0xEB, 0x02, 0xB0, 0x01, 0x3A, 0xC3, 0x74, 0x3C};
    set.expect(BUTTON_AND_READY + sizeof(buttonTest), readyTest);

    // mov cl, [edi + 0x19] / mov eax, [esi - 8]
    set.hook(AFTER_READY, displaced, reinterpret_cast<uintptr_t>(caveMemory));

    // push ecx / mov cl, [edi + 0x18] / push ecx / mov ecx, [esi + 0x14] / push eax / call 0x00993055
    const BYTE castStep[14] = {0x51, 0x8A, 0x4F, 0x18, 0x51, 0x8B, 0x4E, 0x14, 0x50, 0xE8, 0xEA, 0xBB, 0x0D, 0x00};
    set.expect(CAST_STEP, castStep);

    // mov ecx, [esi + 0x14] / mov eax, [ecx] / pop edi / pop esi / pop ebx / jmp [eax + 0x10]
    const BYTE notCast[11] = {0x8B, 0x4E, 0x14, 0x8B, 0x01, 0x5F, 0x5E, 0x5B, 0xFF, 0x60, 0x10};
    set.expect(NOT_CAST, notCast);

    // the whole button state function: the cave relies on the registers it keeps and on its stdcall return
    const BYTE buttonState1[24] = {
        0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x8D, 0xB7, 0xE0, 0x01, 0x00, 0x00, 0x8B, 0xCE, 0xE8, 0xDF, 0x12, 0xB7, 0xFF, 0x84,
        0xC0, 0x8B, 0x5C,
    };
    const BYTE buttonState2[24] = {
        0x24, 0x14, 0x74, 0x10, 0x56, 0x8D, 0x8B, 0x0C, 0x01, 0x00, 0x00, 0xE8, 0x31, 0x0E, 0xD2, 0xFF, 0x84, 0xC0, 0x75, 0x21, 0x8D,
        0xB7, 0x94, 0x01,
    };
    const BYTE buttonState3[24] = {
        0x00, 0x00, 0x8B, 0xCE, 0xE8, 0xBA, 0x12, 0xB7, 0xFF, 0x84, 0xC0, 0x74, 0x14, 0x56, 0x8D, 0x8B, 0x0C, 0x01, 0x00, 0x00, 0xE8,
        0x10, 0x0E, 0xD2,
    };
    const BYTE buttonState4[18] = {
        0xFF, 0x84, 0xC0, 0x75, 0x04, 0x6A, 0x03, 0xEB, 0x02, 0x6A, 0x02, 0x58, 0x5F, 0x5E, 0x5B, 0xC2, 0x08, 0x00,
    };
    set.expect(BUTTON_STATE, buttonState1);
    set.expect(BUTTON_STATE + 24, buttonState2);
    set.expect(BUTTON_STATE + 48, buttonState3);
    set.expect(BUTTON_STATE + 72, buttonState4);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
