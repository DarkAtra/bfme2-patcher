#include <windows.h>
#include <cstdint>

#include "group_ability_conditions.h"
#include "patch_set.h"

// Group ability conditions
//
// Problem: with several units selected, an ability button that may be used by the whole group (OK_FOR_MULTI_EXECUTE) is cast
// by every unit, even by units whose own button is disabled by their state (EnableOnModelCondition, DisableOnModelCondition),
// e.g. a giant that already holds a rock grabbing another one.
// Fix: a unit whose own button is disabled is skipped, as if the player had selected only the others.
//
// The control bar lights a group's button when any member may use it, and the click sends only the power's id and the
// button's options with no unit, so the logic runs the power on the player's whole selection: AIGroup::doSpecialPower
// (0x0076F5DB) and its targeted sibling (0x0077097E) ask every member's gate (0x0082D5DA, 0x0082D925) and cast for each one
// it accepts. The gates check sciences, cost and recharge but are never given the button, so its model conditions are never
// asked again. The two gate calls go through shims that first find the member's own button for the power (its current
// command set, 33 slots, matched by the power's id like the control bar does) and ask the control bar's model condition test
// (0x00942490) about it. A member without such a button (AI, scripts) is gated as before. The walk reads TheControlBar from
// the logic like the game's own logic does (0x0082DE20): the command sets it holds are the INI's, loaded the same on every
// machine, observers included, and the button state only reads the button and the object. Derived from pySAGE's
// multi-execute-gate (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t DO_SPECIAL_POWER_OPTIONS = 0x0076F5FA;
    constexpr uintptr_t DO_SPECIAL_POWER_AT_OPTIONS = 0x007709BA;
    constexpr uintptr_t MEMBER_GATE_ARGUMENTS = 0x0076F6C9;
    constexpr uintptr_t MEMBER_GATE_CALL = 0x0076F6DA;
    constexpr uintptr_t MEMBER_GATE_RESULT = 0x0076F6DF;
    constexpr uintptr_t MEMBER_GATE_AT_ARGUMENTS = 0x00770B54;
    constexpr uintptr_t MEMBER_GATE_AT_CALL = 0x00770B67;
    constexpr uintptr_t MEMBER_GATE_AT_RESULT = 0x00770B6C;
    // thiscall(object, template, ...), ret 0x14
    constexpr uintptr_t MEMBER_GATE = 0x0082D5DA;
    // thiscall(object, ..., template, ...), ret 0x18
    constexpr uintptr_t MEMBER_GATE_AT = 0x0082D925;
    // thiscall(object): the object's current command set name
    constexpr uintptr_t COMMAND_SET_NAME = 0x0069156B;
    // thiscall(control bar, name), ret 4
    constexpr uintptr_t FIND_COMMAND_SET = 0x0071EFA2;
    // thiscall(command set, slot), ret 4
    constexpr uintptr_t COMMAND_BUTTON = 0x0080C837;
    // thiscall(special power template): the INI override in effect
    constexpr uintptr_t FINAL_OVERRIDE = 0x00688D3C;
    // stdcall(button, object): 2 when the button is available for the object, 3 when its model conditions disable it
    constexpr uintptr_t BUTTON_STATE = 0x00942490;
    // the stock walk from an object to its buttons, which the gate repeats
    constexpr uintptr_t STOCK_BUTTON_WALK = 0x0082DE20;

    constexpr BYTE SPECIAL_POWER_COMMAND = 0x18;
    constexpr BYTE SLOTS = 0x21;
    constexpr BYTE DISABLED = 3;
    constexpr BYTE JL = 0x8C;

    // bool gate(Object *object, SpecialPowerTemplate *power), cdecl: false when the object's own button for the power is
    // disabled by the object's model conditions
    void emitGate(Cave &cave) {
        const BYTE prologue[9] = {
            0x55, // push ebp
            0x8B, 0xEC, // mov ebp, esp
            0x53, 0x56, 0x57, // push ebx / push esi / push edi
            0x8B, 0x4D, 0x08 // mov ecx, [ebp + 8]
        };
        cave.emit(prologue);
        cave.call(COMMAND_SET_NAME);
        const BYTE controlBar[8] = {
            0x8B, 0x0D, 0x44, 0x77, 0xDE, 0x00, // mov ecx, [0x00DE7744]: TheControlBar
            0x85, 0xC9 // test ecx, ecx
        };
        cave.emit(controlBar);
        const size_t noControlBar = cave.size;
        cave.jcc(JE, 0);
        cave.emit8(0x50); // push eax
        cave.call(FIND_COMMAND_SET);
        const BYTE commandSetTest[2] = {0x85, 0xC0}; // test eax, eax
        cave.emit(commandSetTest);
        const size_t noCommandSet = cave.size;
        cave.jcc(JE, 0);
        const BYTE firstSlot[4] = {0x8B, 0xF0, 0x33, 0xDB}; // mov esi, eax / xor ebx, ebx
        cave.emit(firstSlot);

        const uintptr_t next = cave.here();
        const BYTE slotArguments[3] = {0x53, 0x8B, 0xCE}; // push ebx / mov ecx, esi
        cave.emit(slotArguments);
        cave.call(COMMAND_BUTTON);
        const BYTE buttonTest[4] = {0x8B, 0xF8, 0x85, 0xFF}; // mov edi, eax / test edi, edi
        cave.emit(buttonTest);
        const size_t emptySlot = cave.size;
        cave.jcc(JE, 0);
        const BYTE commandTest[4] = {0x83, 0x7F, 0x14, SPECIAL_POWER_COMMAND}; // cmp dword [edi + 0x14], 0x18
        cave.emit(commandTest);
        const size_t otherCommand = cave.size;
        cave.jcc(JNE, 0);
        const BYTE powerTest[5] = {0x8B, 0x4F, 0x44, 0x85, 0xC9}; // mov ecx, [edi + 0x44] / test ecx, ecx
        cave.emit(powerTest);
        const size_t noPower = cave.size;
        cave.jcc(JE, 0);
        cave.call(FINAL_OVERRIDE);
        const BYTE samePower[9] = {
            0x8B, 0x40, 0x14, // mov eax, [eax + 0x14]: the button's power id
            0x8B, 0x4D, 0x0C, // mov ecx, [ebp + 0xC]
            0x3B, 0x41, 0x14 // cmp eax, [ecx + 0x14]
        };
        cave.emit(samePower);
        const size_t otherPower = cave.size;
        cave.jcc(JNE, 0);
        const BYTE stateArguments[4] = {0xFF, 0x75, 0x08, 0x57}; // push [ebp + 8] / push edi
        cave.emit(stateArguments);
        cave.call(BUTTON_STATE);
        const BYTE disabledTest[3] = {0x83, 0xF8, DISABLED}; // cmp eax, 3
        cave.emit(disabledTest);
        const size_t notDisabled = cave.size;
        cave.jcc(JNE, 0);
        const BYTE refuse[2] = {0x32, 0xC0}; // xor al, al
        cave.emit(refuse);
        const size_t refused = cave.size;
        cave.jmp(0);

        const uintptr_t step = cave.here();
        const BYTE nextSlot[4] = {0x43, 0x83, 0xFB, SLOTS}; // inc ebx / cmp ebx, 33
        cave.emit(nextSlot);
        cave.jcc(JL, next);

        const uintptr_t allow = cave.here();
        const BYTE allowed[2] = {0xB0, 0x01}; // mov al, 1
        cave.emit(allowed);
        const uintptr_t done = cave.here();
        const BYTE epilogue[5] = {0x5F, 0x5E, 0x5B, 0x5D, 0xC3}; // pop edi / pop esi / pop ebx / pop ebp / ret
        cave.emit(epilogue);

        cave.resolveJcc(noControlBar, allow);
        cave.resolveJcc(noCommandSet, allow);
        cave.resolveJcc(emptySlot, step);
        cave.resolveJcc(otherCommand, step);
        cave.resolveJcc(noPower, step);
        cave.resolveJcc(otherPower, step);
        cave.resolveJcc(notDisabled, allow);
        cave.resolveJmp(refused, done);
    }

    // Stands in for a call to a member gate whose arguments are pushed and whose this is in ecx: asks the gate above, then
    // either refuses (cleaning the arguments like the member gate would) or continues to the member gate.
    void emitShim(Cave &cave, const uintptr_t gate, const uintptr_t memberGate, const BYTE argumentBytes, const BYTE objectAt,
                  const BYTE powerAt) {
        const BYTE arguments[9] = {
            0x51, // push ecx
            0xFF, 0x74, 0x24, static_cast<BYTE>(powerAt + 4), // push dword [esp + power]
            0xFF, 0x74, 0x24, static_cast<BYTE>(objectAt + 8) // push dword [esp + object]
        };
        cave.emit(arguments);
        cave.call(gate);
        const BYTE result[8] = {
            0x83, 0xC4, 0x08, // add esp, 8
            0x59, // pop ecx
            0x84, 0xC0, // test al, al
            0x75, 0x05 // jne memberGate
        };
        cave.emit(result);
        const BYTE refuse[5] = {0x32, 0xC0, 0xC2, argumentBytes, 0x00}; // xor al, al / ret argumentBytes
        cave.emit(refuse);
        cave.jmp(memberGate);
    }
}

bool applyGroupAbilityConditions() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);
    const uintptr_t gate = cave.here();
    emitGate(cave);
    const uintptr_t shim = cave.here();
    emitShim(cave, gate, MEMBER_GATE, 0x14, 0x04, 0x08);
    const uintptr_t shimAt = cave.here();
    emitShim(cave, gate, MEMBER_GATE_AT, 0x18, 0x04, 0x10);

    static PatchSet set;
    set.reset();

    // mov eax, [ebp + 0xC] / ... / and eax, 0x100000: OK_FOR_MULTI_EXECUTE sends the group loop to every member
    const BYTE options[16] = {0x8B, 0x45, 0x0C, 0x33, 0xFF, 0x25, 0x00, 0x00, 0x10, 0x00, 0x89, 0x7D, 0x08, 0x89, 0x45, 0xF4};
    set.expect(DO_SPECIAL_POWER_OPTIONS, options);
    const BYTE optionsAt[11] = {0x8B, 0x45, 0x10, 0x25, 0x00, 0x00, 0x10, 0x00, 0x89, 0x45, 0xE0};
    set.expect(DO_SPECIAL_POWER_AT_OPTIONS, optionsAt);

    // mov ecx, [0x00DE8BA8] / push 1 / push [ebp + 0xC] / push 0 / push [ebp - 4] (the power) / push esi (the member)
    const BYTE memberGateArguments[17] = {0x8B, 0x0D, 0xA8, 0x8B, 0xDE, 0x00, 0x6A, 0x01, 0xFF, 0x75, 0x0C, 0x6A, 0x00, 0xFF, 0x75, 0xFC, 0x56};
    set.expect(MEMBER_GATE_ARGUMENTS, memberGateArguments);
    const BYTE memberGateCall[5] = {0xE8, 0xFB, 0xDE, 0x0B, 0x00};
    set.redirectCall(MEMBER_GATE_CALL, memberGateCall, shim);
    // test al, al / je next member
    const BYTE memberGateResult[4] = {0x84, 0xC0, 0x74, 0x21};
    set.expect(MEMBER_GATE_RESULT, memberGateResult);

    // push [ebp - 0x1C] / mov ecx, [0x00DE8BA8] / push [ebp + 0x10] / push ebx (the power) / push 0 / push [ebp + 0xC] /
    // push esi (the member)
    const BYTE memberGateAtArguments[19] = {
        0xFF, 0x75, 0xE4, 0x8B, 0x0D, 0xA8, 0x8B, 0xDE, 0x00, 0xFF, 0x75, 0x10, 0x53, 0x6A, 0x00, 0xFF, 0x75, 0x0C, 0x56
    };
    set.expect(MEMBER_GATE_AT_ARGUMENTS, memberGateAtArguments);
    const BYTE memberGateAtCall[5] = {0xE8, 0xB9, 0xCD, 0x0B, 0x00};
    set.redirectCall(MEMBER_GATE_AT_CALL, memberGateAtCall, shimAt);
    const BYTE memberGateAtResult[4] = {0x84, 0xC0, 0x74, 0x19};
    set.expect(MEMBER_GATE_AT_RESULT, memberGateAtResult);

    // push ebp / mov ebp, esp / push esi / push edi / mov edi, [ebp + 0xC]
    const BYTE memberGate[8] = {0x55, 0x8B, 0xEC, 0x56, 0x57, 0x8B, 0x7D, 0x0C};
    set.expect(MEMBER_GATE, memberGate);
    // mov eax, 0x00B9BE2D / call: the exception frame's setup
    const BYTE memberGateAt[5] = {0xB8, 0x2D, 0xBE, 0xB9, 0x00};
    set.expect(MEMBER_GATE_AT, memberGateAt);

    // call COMMAND_SET_NAME / mov ecx, [TheControlBar] / push eax / call FIND_COMMAND_SET, then per slot: call COMMAND_BUTTON,
    // cmp dword [esi + 0x14], 0x18, mov ecx, [esi + 0x44], call FINAL_OVERRIDE
    const BYTE buttonWalk[17] = {0xE8, 0x46, 0x37, 0xE6, 0xFF, 0x8B, 0x0D, 0x44, 0x77, 0xDE, 0x00, 0x50, 0xE8, 0x71, 0x11, 0xEF, 0xFF};
    set.expect(STOCK_BUTTON_WALK, buttonWalk);
    const BYTE buttonSlot[24] = {
        0xE8, 0xF0, 0xE9, 0xFD, 0xFF, 0x8B, 0xF0, 0x85, 0xF6, 0x74, 0x43, 0x83, 0x7E, 0x14, 0x18, 0x75, 0x3D, 0x8B, 0x4E, 0x44,
        0xE8, 0xE1, 0xAE, 0xE5
    };
    set.expect(STOCK_BUTTON_WALK + 0x22, buttonSlot);

    // push ebx / push esi / push edi / mov edi, [esp + 0x10] (the button) / lea esi, [edi + 0x1E0] (DisableOnModelCondition)
    const BYTE buttonState[13] = {0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x8D, 0xB7, 0xE0, 0x01, 0x00, 0x00};
    set.expect(BUTTON_STATE, buttonState);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
