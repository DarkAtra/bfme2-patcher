#include <windows.h>
#include <cstdint>

#include "group_ability_cooldown.h"
#include "patch_set.h"

// Group ability cooldown
//
// Problem: with several units selected, an ability that the whole group may use (SPECIAL_POWER with OK_FOR_MULTI_EXECUTE) shows
// one unit's cooldown and cannot be clicked once that unit has used it, though the others still have it ready.
// Fix: the button shows ready and can be clicked while any selected unit has the power ready; the click fires it on the ready
// units only, which the logic already does (AIGroup::doSpecialPower asks every member's gate with "check recharge" set,
// 0x0076F6CF), so this patch only changes what the local player sees and may click.
//
// A selection of one kind is painted from one representative: the single object slot update asks the availability evaluator
// (0x00942733) about it alone (0x00943ADB), and answer 5 paints a disabled button with that unit's recharge clock. The shim
// there asks the rest of the selection (TheInGameUI vtable + 0x124, a list of drawables) when the answer is 5: the first unit
// that answers usable (1 or 2) decides, without a clock; when none does, the clock closest to ready is shown. The click gate
// of ControlBar::doCommand (0x0094070F) asks CommandButton::isReady (0x0075CC3E) of the representative, or of every member of
// a mixed selection; its two calls go through a shim that answers ready when any selected unit is. A mixed selection
// (0x00944911) paints every member into the same window in turn, so a recharging member leaves its status bit (0x400000) and
// its one frame clock request (kind 2 in the window's data, 0x00729F0C) on a slot a ready member made usable; the shim on the
// usable branch's last clearStatus (0x00944B67) removes both, except on special powers whose click still needs every member
// ready (0x17, or 0x18 without OK_FOR_MULTI_EXECUTE). pySAGE's shim on the mixed selection's availability call (0x009449D6)
// is left out: it asks every unit again for every unit, and the slot only differs in which recharging unit's clock it shows
// when none is ready. Derived from pySAGE's multi-execute-cooldown (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t SLOT_AVAILABILITY_ARGUMENTS = 0x00943AC9;
    constexpr uintptr_t SLOT_AVAILABILITY_CALL = 0x00943ADB;
    constexpr uintptr_t READY_GATE = 0x0094070F;
    constexpr uintptr_t READY_CALL = 0x00940716;
    constexpr uintptr_t MULTI_READY_CALL = 0x00940762;
    constexpr uintptr_t MULTI_USABLE_PASS = 0x00944B45;
    constexpr uintptr_t MULTI_USABLE_CLEAR_CALL = 0x00944B67;
    // thiscall(control bar, button, window, object, progress, flag), ret 0x14
    constexpr uintptr_t GET_AVAILABILITY = 0x00942733;
    // thiscall(button, object), ret 4
    constexpr uintptr_t IS_READY = 0x0075CC3E;
    // thiscall(window, mask), ret 4
    constexpr uintptr_t CLEAR_STATUS = 0x007155B9;
    // thiscall(window): the window's data
    constexpr uintptr_t GET_DATA = 0x009A5E08;
    // cdecl(window): the window's command button
    constexpr uintptr_t GET_COMMAND_BUTTON = 0x0072979C;

    constexpr BYTE JBE = 0x86;

    size_t forwardJcc(Cave &cave, const BYTE condition) {
        const size_t offset = cave.size;
        cave.jcc(condition, 0);
        return offset;
    }

    size_t forwardJmp(Cave &cave) {
        const size_t offset = cave.size;
        cave.jmp(0);
        return offset;
    }

    // jumps to the returned jcc unless the button in `reg` (modrm register number) is a SPECIAL_POWER with OK_FOR_MULTI_EXECUTE
    void emitMultiExecutePower(Cave &cave, const BYTE reg, size_t (&fail)[2]) {
        const BYTE command[4] = {0x83, static_cast<BYTE>(0x78 | reg), 0x14, 0x18}; // cmp dword [reg + 0x14], 0x18
        cave.emit(command);
        fail[0] = forwardJcc(cave, JNE);
        const BYTE options[7] = {0xF7, static_cast<BYTE>(0x40 | reg), 0x1C, 0x00, 0x00, 0x10, 0x00}; // test dword [reg + 0x1C], 0x100000
        cave.emit(options);
        fail[1] = forwardJcc(cave, JE);
    }

    // eax = the selected drawables' list head, or a jump to the returned jccs
    void emitSelection(Cave &cave, size_t (&empty)[2]) {
        const BYTE inGameUi[8] = {
            0x8B, 0x0D, 0x30, 0x48, 0xDE, 0x00, // mov ecx, [0x00DE4830]: TheInGameUI
            0x85, 0xC9 // test ecx, ecx
        };
        cave.emit(inGameUi);
        empty[0] = forwardJcc(cave, JE);
        const BYTE selection[10] = {
            0x8B, 0x01, // mov eax, [ecx]
            0xFF, 0x90, 0x24, 0x01, 0x00, 0x00, // call [eax + 0x124]: the selected drawables
            0x85, 0xC0 // test eax, eax
        };
        cave.emit(selection);
        empty[1] = forwardJcc(cave, JE);
        const BYTE head[2] = {0x8B, 0x00}; // mov eax, [eax]
        cave.emit(head);
    }

    // eax = the object of the list node in esi, or a jump to the returned jccs
    void emitMemberObject(Cave &cave, size_t (&skip)[2]) {
        const BYTE drawable[5] = {
            0x8B, 0x46, 0x08, // mov eax, [esi + 8]: the drawable
            0x85, 0xC0 // test eax, eax
        };
        cave.emit(drawable);
        skip[0] = forwardJcc(cave, JE);
        const BYTE object[8] = {
            0x8B, 0x80, 0xFC, 0x00, 0x00, 0x00, // mov eax, [eax + 0xFC]: its object
            0x85, 0xC0 // test eax, eax
        };
        cave.emit(object);
        skip[1] = forwardJcc(cave, JE);
    }

    void resolve(Cave &cave, const size_t (&jccs)[2], const uintptr_t target) {
        cave.resolveJcc(jccs[0], target);
        cave.resolveJcc(jccs[1], target);
    }
}

bool applyGroupAbilityCooldown() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);
    constexpr BYTE EDX = 2;
    constexpr BYTE EBX = 3;

    // Stands in for the evaluator at the single object slot update, same arguments, ret 0x14. ebx the control bar, edi the
    // answer, [ebp - 0x10] a member's progress, [ebp - 0x14] the best progress, [ebp - 0x18] the list head.
    const uintptr_t availabilityAny = cave.here();
    const BYTE availabilityPrologue[23] = {
        0x55, // push ebp
        0x8B, 0xEC, // mov ebp, esp
        0x53, 0x56, 0x57, // push ebx / push esi / push edi
        0x83, 0xEC, 0x0C, // sub esp, 0xC
        0x8B, 0xD9, // mov ebx, ecx
        0xFF, 0x75, 0x18, // push [ebp + 0x18]
        0xFF, 0x75, 0x14, // push [ebp + 0x14]
        0xFF, 0x75, 0x10, // push [ebp + 0x10]
        0xFF, 0x75, 0x0C // push [ebp + 0xC]
    };
    cave.emit(availabilityPrologue);
    const BYTE button[3] = {0xFF, 0x75, 0x08}; // push [ebp + 8]
    cave.emit(button);
    cave.call(GET_AVAILABILITY);
    const BYTE recharging[3] = {0x83, 0xF8, 0x05}; // cmp eax, 5
    cave.emit(recharging);
    const size_t notRecharging = forwardJcc(cave, JNE);
    const BYTE buttonTest[5] = {
        0x8B, 0x55, 0x08, // mov edx, [ebp + 8]
        0x85, 0xD2 // test edx, edx
    };
    cave.emit(buttonTest);
    const size_t noButton = forwardJcc(cave, JE);
    size_t notMultiExecute[2];
    emitMultiExecutePower(cave, EDX, notMultiExecute);
    const BYTE ownProgress[10] = {
        0x8B, 0xF8, // mov edi, eax
        0x8B, 0x4D, 0x14, // mov ecx, [ebp + 0x14]
        0x8B, 0x01, // mov eax, [ecx]
        0x89, 0x45, 0xEC // mov [ebp - 0x14], eax: the best progress so far
    };
    cave.emit(ownProgress);
    size_t noSelection[2];
    emitSelection(cave, noSelection);
    const BYTE firstNode[5] = {
        0x89, 0x45, 0xE8, // mov [ebp - 0x18], eax
        0x8B, 0x30 // mov esi, [eax]
    };
    cave.emit(firstNode);

    const uintptr_t availabilityWalk = cave.here();
    const BYTE walkEnd[3] = {0x3B, 0x75, 0xE8}; // cmp esi, [ebp - 0x18]
    cave.emit(walkEnd);
    const size_t walked = forwardJcc(cave, JE);
    size_t noMember[2];
    emitMemberObject(cave, noMember);
    const BYTE askedAlready[3] = {0x3B, 0x45, 0x10}; // cmp eax, [ebp + 0x10]
    cave.emit(askedAlready);
    const size_t sameMember = forwardJcc(cave, JE);
    const BYTE askMember[16] = {
        0x8D, 0x4D, 0xF0, // lea ecx, [ebp - 0x10]
        0xFF, 0x75, 0x18, // push [ebp + 0x18]
        0x51, // push ecx
        0x50, // push eax
        0xFF, 0x75, 0x0C, // push [ebp + 0xC]
        0xFF, 0x75, 0x08, // push [ebp + 8]
        0x8B, 0xCB // mov ecx, ebx
    };
    cave.emit(askMember);
    cave.call(GET_AVAILABILITY);
    const BYTE usableLow[3] = {0x83, 0xF8, 0x01}; // cmp eax, 1
    cave.emit(usableLow);
    const size_t readyLow = forwardJcc(cave, JE);
    const BYTE usableHigh[3] = {0x83, 0xF8, 0x02}; // cmp eax, 2
    cave.emit(usableHigh);
    const size_t readyHigh = forwardJcc(cave, JE);
    cave.emit(recharging);
    const size_t otherAnswer = forwardJcc(cave, JNE);
    const BYTE closer[9] = {
        0xF3, 0x0F, 0x10, 0x45, 0xF0, // movss xmm0, [ebp - 0x10]
        0x0F, 0x2F, 0x45, 0xEC // comiss xmm0, [ebp - 0x14]
    };
    cave.emit(closer);
    const size_t notCloser = forwardJcc(cave, JBE);
    const BYTE best[5] = {0xF3, 0x0F, 0x11, 0x45, 0xEC}; // movss [ebp - 0x14], xmm0
    cave.emit(best);

    const uintptr_t availabilityNext = cave.here();
    const BYTE nextNode[2] = {0x8B, 0x36}; // mov esi, [esi]
    cave.emit(nextNode);
    cave.jmp(availabilityWalk);

    const uintptr_t availabilityReady = cave.here();
    const BYTE ready[11] = {
        0x8B, 0xF8, // mov edi, eax
        0x8B, 0x4D, 0x14, // mov ecx, [ebp + 0x14]
        0xC7, 0x01, 0x00, 0x00, 0x80, 0x3F // mov dword [ecx], 1.0f: no clock
    };
    cave.emit(ready);
    const size_t readyDone = forwardJmp(cave);

    const uintptr_t availabilityBest = cave.here();
    const BYTE bestProgress[8] = {
        0x8B, 0x4D, 0x14, // mov ecx, [ebp + 0x14]
        0x8B, 0x45, 0xEC, // mov eax, [ebp - 0x14]
        0x89, 0x01 // mov [ecx], eax: the clock closest to ready
    };
    cave.emit(bestProgress);

    const uintptr_t availabilityKeep = cave.here();
    const BYTE answer[2] = {0x8B, 0xC7}; // mov eax, edi
    cave.emit(answer);
    const uintptr_t availabilityOut = cave.here();
    const BYTE availabilityEpilogue[10] = {
        0x8D, 0x65, 0xF4, // lea esp, [ebp - 0xC]
        0x5F, 0x5E, 0x5B, // pop edi / pop esi / pop ebx
        0x5D, // pop ebp
        0xC2, 0x14, 0x00 // ret 0x14
    };
    cave.emit(availabilityEpilogue);

    cave.resolveJcc(notRecharging, availabilityOut);
    cave.resolveJcc(noButton, availabilityOut);
    resolve(cave, notMultiExecute, availabilityOut);
    resolve(cave, noSelection, availabilityKeep);
    cave.resolveJcc(walked, availabilityBest);
    resolve(cave, noMember, availabilityNext);
    cave.resolveJcc(sameMember, availabilityNext);
    cave.resolveJcc(readyLow, availabilityReady);
    cave.resolveJcc(readyHigh, availabilityReady);
    cave.resolveJcc(otherAnswer, availabilityNext);
    cave.resolveJcc(notCloser, availabilityNext);
    cave.resolveJmp(readyDone, availabilityKeep);

    // Stands in for CommandButton::isReady at the click gate: ecx the button, [esp + 4] the object, ret 4. ebx the button,
    // edi the list head, esi the node.
    const uintptr_t readyAny = cave.here();
    const BYTE readyPrologue[11] = {
        0x55, // push ebp
        0x8B, 0xEC, // mov ebp, esp
        0x53, 0x56, 0x57, // push ebx / push esi / push edi
        0x8B, 0xD9, // mov ebx, ecx
        0xFF, 0x75, 0x08 // push [ebp + 8]
    };
    cave.emit(readyPrologue);
    cave.call(IS_READY);
    const BYTE readyTest[2] = {0x84, 0xC0}; // test al, al
    cave.emit(readyTest);
    const size_t ownReady = forwardJcc(cave, JNE);
    size_t readyNotMultiExecute[2];
    emitMultiExecutePower(cave, EBX, readyNotMultiExecute);
    size_t readyNoSelection[2];
    emitSelection(cave, readyNoSelection);
    const BYTE readyFirstNode[4] = {
        0x8B, 0xF8, // mov edi, eax
        0x8B, 0x37 // mov esi, [edi]
    };
    cave.emit(readyFirstNode);
    const uintptr_t readyWalk = cave.here();
    const BYTE readyWalkEnd[2] = {0x3B, 0xF7}; // cmp esi, edi
    cave.emit(readyWalkEnd);
    const size_t readyWalked = forwardJcc(cave, JE);
    size_t readyNoMember[2];
    emitMemberObject(cave, readyNoMember);
    const BYTE askReady[3] = {
        0x50, // push eax
        0x8B, 0xCB // mov ecx, ebx
    };
    cave.emit(askReady);
    cave.call(IS_READY);
    cave.emit(readyTest);
    const size_t memberReady = forwardJcc(cave, JNE);
    const uintptr_t readyNext = cave.here();
    cave.emit(nextNode);
    cave.jmp(readyWalk);
    const uintptr_t readyYes = cave.here();
    const BYTE yes[4] = {
        0xB0, 0x01, // mov al, 1
        0xEB, 0x02 // jmp out
    };
    cave.emit(yes);
    const uintptr_t readyNo = cave.here();
    const BYTE no[2] = {0x32, 0xC0}; // xor al, al
    cave.emit(no);
    const BYTE readyEpilogue[7] = {
        0x5F, 0x5E, 0x5B, // out: pop edi / pop esi / pop ebx
        0x5D, // pop ebp
        0xC2, 0x04, 0x00 // ret 4
    };
    cave.emit(readyEpilogue);

    cave.resolveJcc(ownReady, readyYes);
    resolve(cave, readyNotMultiExecute, readyNo);
    resolve(cave, readyNoSelection, readyNo);
    cave.resolveJcc(readyWalked, readyNo);
    resolve(cave, readyNoMember, readyNext);
    cave.resolveJcc(memberReady, readyYes);

    // Stands in for the last clearStatus of a usable slot in the mixed selection's final pass: ecx the window, [esp + 4] the
    // mask, ret 4. esi the window.
    const uintptr_t usablePaint = cave.here();
    const BYTE usablePrologue[7] = {
        0x56, // push esi
        0x8B, 0xF1, // mov esi, ecx
        0xFF, 0x74, 0x24, 0x08 // push [esp + 8]
    };
    cave.emit(usablePrologue);
    cave.call(CLEAR_STATUS);
    cave.emit8(0x56); // push esi
    cave.call(GET_COMMAND_BUTTON);
    const BYTE buttonFound[3] = {
        0x59, // pop ecx
        0x85, 0xC0 // test eax, eax
    };
    cave.emit(buttonFound);
    const size_t usableNoButton = forwardJcc(cave, JE);
    const BYTE fromCommandCenter[4] = {0x83, 0x78, 0x14, 0x17}; // cmp dword [eax + 0x14], 0x17: its click needs every member
    cave.emit(fromCommandCenter);
    const size_t keepClock = forwardJcc(cave, JE);
    const BYTE specialPower[4] = {0x83, 0x78, 0x14, 0x18}; // cmp dword [eax + 0x14], 0x18
    cave.emit(specialPower);
    const size_t otherCommand = forwardJcc(cave, JNE);
    const BYTE multiExecute[7] = {0xF7, 0x40, 0x1C, 0x00, 0x00, 0x10, 0x00}; // test dword [eax + 0x1C], 0x100000
    cave.emit(multiExecute);
    const size_t notMultiExecuteClock = forwardJcc(cave, JE);
    const uintptr_t clear = cave.here();
    const BYTE clearRecharging[7] = {
        0x68, 0x00, 0x00, 0x40, 0x00, // push 0x400000: recharging
        0x8B, 0xCE // mov ecx, esi
    };
    cave.emit(clearRecharging);
    cave.call(CLEAR_STATUS);
    const BYTE window[2] = {0x8B, 0xCE}; // mov ecx, esi
    cave.emit(window);
    cave.call(GET_DATA);
    const BYTE dataFound[2] = {0x85, 0xC0}; // test eax, eax
    cave.emit(dataFound);
    const size_t noData = forwardJcc(cave, JE);
    const BYTE clockRequest[3] = {0x80, 0x38, 0x02}; // cmp byte [eax], 2: a clock request
    cave.emit(clockRequest);
    const size_t noClock = forwardJcc(cave, JNE);
    const BYTE dropClock[3] = {0xC6, 0x00, 0x00}; // mov byte [eax], 0
    cave.emit(dropClock);
    const uintptr_t usableOut = cave.here();
    const BYTE usableEpilogue[4] = {
        0x5E, // pop esi
        0xC2, 0x04, 0x00 // ret 4
    };
    cave.emit(usableEpilogue);

    cave.resolveJcc(usableNoButton, usableOut);
    cave.resolveJcc(keepClock, usableOut);
    cave.resolveJcc(otherCommand, clear);
    cave.resolveJcc(notMultiExecuteClock, usableOut);
    cave.resolveJcc(noData, usableOut);
    cave.resolveJcc(noClock, usableOut);

    static PatchSet set;
    set.reset();

    // push 0 / lea ecx, [ebp - 0x28] / push ecx / push [ebp - 0x1C] / mov ecx, edi / push esi / push eax / movss [ebp - 0x28], xmm0
    const BYTE slotArguments[18] = {0x6A, 0x00, 0x8D, 0x4D, 0xD8, 0x51, 0xFF, 0x75, 0xE4, 0x8B, 0xCF, 0x56, 0x50, 0xF3, 0x0F, 0x11, 0x45, 0xD8};
    set.expect(SLOT_AVAILABILITY_ARGUMENTS, slotArguments);
    const BYTE slotCall[5] = {0xE8, 0x53, 0xEC, 0xFF, 0xFF};
    set.redirectCall(SLOT_AVAILABILITY_CALL, slotCall, availabilityAny);

    // test ebx, ebx / je / push ebx / mov ecx, esi
    const BYTE readyGate[7] = {0x85, 0xDB, 0x74, 0x15, 0x53, 0x8B, 0xCE};
    set.expect(READY_GATE, readyGate);
    const BYTE readyCall[5] = {0xE8, 0x23, 0xC5, 0xE1, 0xFF};
    set.redirectCall(READY_CALL, readyCall, readyAny);
    // test al, al / jne post / jmp drop / cmp dword [edi + 0x70], 7: the mixed context / jne / mov ecx, [TheInGameUI] ...
    const BYTE readyResult[24] = {
        0x84, 0xC0, 0x0F, 0x85, 0x33, 0xFF, 0xFF, 0xFF, 0xE9, 0x1A, 0xFF, 0xFF, 0xFF, 0x83, 0x7F, 0x70, 0x07, 0x0F, 0x85, 0x24,
        0xFF, 0xFF, 0xFF, 0x8B
    };
    set.expect(READY_CALL + 5, readyResult);
    // ... / mov eax, [ecx] / call [eax + 0x124]: the selected drawables / test eax, eax / mov [ebp - 0x18], eax / je
    const BYTE readySelection[24] = {
        0x0D, 0x30, 0x48, 0xDE, 0x00, 0x8B, 0x01, 0xFF, 0x90, 0x24, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x89, 0x45, 0xE8, 0x0F, 0x84,
        0xF7, 0xFE, 0xFF, 0xFF
    };
    set.expect(READY_CALL + 29, readySelection);
    // mov eax, [eax] / mov edi, [eax] / cmp edi, eax / je / mov eax, [edi + 8] / push [eax + 0xFC] / mov ecx, esi
    const BYTE readyMembers[23] = {
        0x8B, 0x00, 0x8B, 0x38, 0x3B, 0xF8, 0x0F, 0x84, 0xEB, 0xFE, 0xFF, 0xFF, 0x8B, 0x47, 0x08, 0xFF, 0xB0, 0xFC, 0x00, 0x00,
        0x00, 0x8B, 0xCE
    };
    set.expect(READY_CALL + 53, readyMembers);
    const BYTE multiReadyCall[5] = {0xE8, 0xD7, 0xC4, 0xE1, 0xFF};
    set.redirectCall(MULTI_READY_CALL, multiReadyCall, readyAny);
    // test al, al / je drop / mov edi, [edi] / mov eax, [ebp - 0x18] / cmp edi, [eax] / jne / jmp post
    const BYTE multiReadyResult[20] = {
        0x84, 0xC0, 0x0F, 0x84, 0xD3, 0xFE, 0xFF, 0xFF, 0x8B, 0x3F, 0x8B, 0x45, 0xE8, 0x3B, 0x38, 0x75, 0xDF, 0xE9, 0xD9, 0xFE
    };
    set.expect(MULTI_READY_CALL + 5, multiReadyResult);

    // cmp [esp + ebp * 4 + 0x2C], ebx: usable members / mov ecx, esi / jle disable / push 1 / call enable /
    // push 0x80000000 / mov ecx, esi / call clearStatus / push 0x1000000 / mov ecx, esi
    const BYTE usablePass[24] = {
        0x39, 0x5C, 0xAC, 0x2C, 0x8B, 0xCE, 0x7E, 0x21, 0x6A, 0x01, 0xE8, 0x5F, 0x09, 0xDD, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x80,
        0x8B, 0xCE, 0xE8, 0x59
    };
    set.expect(MULTI_USABLE_PASS, usablePass);
    const BYTE usablePassEnd[10] = {0x0A, 0xDD, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x01, 0x8B, 0xCE};
    set.expect(MULTI_USABLE_PASS + 24, usablePassEnd);
    const BYTE usableClearCall[5] = {0xE8, 0x4D, 0x0A, 0xDD, 0xFF};
    set.redirectCall(MULTI_USABLE_CLEAR_CALL, usableClearCall, usablePaint);
    const BYTE usableNext[2] = {0xEB, 0x06}; // jmp next slot
    set.expect(MULTI_USABLE_CLEAR_CALL + 5, usableNext);

    // mov eax, 0x00BAC41E / call: the exception frame's setup
    const BYTE getAvailability[10] = {0xB8, 0x1E, 0xC4, 0xBA, 0x00, 0xE8, 0xB3, 0xA7, 0x0F, 0x00};
    set.expect(GET_AVAILABILITY, getAvailability);
    // push esi / push edi / mov edi, [esp + 0xC] / test edi, edi
    const BYTE isReady[8] = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x85, 0xFF};
    set.expect(IS_READY, isReady);
    // mov edx, [esp + 4] / mov eax, [ecx + 8] / not edx / and edx, eax / mov [ecx + 8], edx / ret 4
    const BYTE clearStatus[17] = {0x8B, 0x54, 0x24, 0x04, 0x8B, 0x41, 0x08, 0xF7, 0xD2, 0x23, 0xD0, 0x89, 0x51, 0x08, 0xC2, 0x04, 0x00};
    set.expect(CLEAR_STATUS, clearStatus);
    // mov eax, [ecx + 0x2C] / ret
    const BYTE getData[4] = {0x8B, 0x41, 0x2C, 0xC3};
    set.expect(GET_DATA, getData);
    // mov ecx, [esp + 4] / test ecx, ecx / je
    const BYTE getCommandButton[8] = {0x8B, 0x4C, 0x24, 0x04, 0x85, 0xC9, 0x74, 0x09};
    set.expect(GET_COMMAND_BUTTON, getCommandButton);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
