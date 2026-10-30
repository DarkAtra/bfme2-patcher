#include <windows.h>
#include <cstdint>

#include "cooldown_modifiers.h"
#include "patch_set.h"

// Cooldowns follow recharge modifiers
//
// Problem: a recharge modifier (a RECHARGE_TIME aura or buff, a SpellRechargeModifierUpgrade) gained or lost after an ability
// was cast does not change the cooldown already running; it only counts from the next cast.
// Fix: once per logic frame every running cooldown is compared with the cooldown a cast would get now, and when the two differ
// the rest of it is rescaled, so a modifier counts for exactly the time it is held.
//
// startPowerRecharge (0x00896E31, slot + 0x3C of 23 special power module vtables) computes the cooldown once at the cast:
// frames = max(1, ftol(ReloadTime * player discount * RECHARGE_TIME multiplier)), and stores the ready frame (interface + 8) and
// those frames (+ 4, the button clock's denominator). The sweep runs where the logic frame counter advances (0x0062E56A: when
// the frame gate answers yes and bl is 0, so the same frames on every machine), walks the logic's object list (TheGameLogic +
// 0xAC, linked through Object + 0x8C) and each object's modules, and for every special power on cooldown repeats the cast's
// formula with today's modifiers. When the result differs from the stored frames, the remaining frames are scaled by
// new / old (at least 1) and the new frames stored, so the button clock keeps its position and changes only its speed.
// A partial recharge (startPowerRecharge's argument below 1, 0x00896F16) stores only a ready frame computed from today's frames
// and leaves + 4 at the last full recharge's: the hook there stores today's frames too, so + 4 always belongs to the running
// cooldown (otherwise the sweep would apply a changed modifier a second time) and the button clock divides by the right frames.
// Left alone: paused cooldowns (+ 0xC), SharedSyncedTimer powers (template + 0x59), and the second startPowerRecharge
// (0x00991500, 3 vtables), which keeps no duration. With no recharge modifier changing during a cooldown nothing changes. The
// sweep costs one indirect call per module per logic frame. Derived from pySAGE's recharge-rescale
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t FRAME_GATE_CALL = 0x0062E56A;
    // thiscall(game logic): whether the logic frame advances
    constexpr uintptr_t FRAME_GATE = 0x00625130;
    constexpr uintptr_t OBJECT_LIST_WALK = 0x0062E908;
    constexpr uintptr_t OBJECT_LIST_NEXT = 0x0062E92B;
    constexpr uintptr_t GET_SPECIAL_POWER = 0x008979F5;
    constexpr uintptr_t MODULE_WALK = 0x0068BDD0;
    constexpr uintptr_t START_POWER_RECHARGE = 0x00896E31;
    constexpr uintptr_t START_POWER_RECHARGE_OTHER = 0x00991500;
    constexpr uintptr_t START_POWER_RECHARGE_SLOT = 0x00C6502C;
    constexpr uintptr_t START_POWER_RECHARGE_OTHER_SLOT = 0x00C6512C;
    constexpr uintptr_t PARTIAL_RECHARGE = 0x00896F4B;
    constexpr uintptr_t PARTIAL_READY_FRAME_STORE = 0x00896F70;
    constexpr uintptr_t RECHARGE_DONE = 0x00896F8E;
    constexpr uintptr_t IS_READY = 0x00896CD4;
    constexpr uintptr_t PERCENT_READY = 0x00896D8F;
    // thiscall(object, type, Real *out, ctx, flag), ret 0x10; leaves out alone for an object that was never modified
    constexpr uintptr_t GET_MODIFIER_MULTIPLIER = 0x0068C82D;
    constexpr uintptr_t MODIFIER_HOLDER = 0x0068C4A6;
    // thiscall(object): the controlling player or null
    constexpr uintptr_t GET_CONTROLLING_PLAYER = 0x0068B678;
    // thiscall(template): the INI override in effect
    constexpr uintptr_t FINAL_OVERRIDE = 0x00688D3C;
    // thiscall(player): fld [player + 0x718], the spell book's recharge discount
    constexpr uintptr_t PLAYER_RECHARGE_DISCOUNT = 0x006AAAD2;
    // pops st(0) into eax
    constexpr uintptr_t FTOL = 0x00A3CFA4;

    constexpr BYTE JLE = 0x8E;

    size_t forwardJcc(Cave &cave, const BYTE condition) {
        const size_t offset = cave.size;
        cave.jcc(condition, 0);
        return offset;
    }

    // power(eax = special power interface, esi = object). Frame: -4 the RECHARGE_TIME multiplier, -8 the player's discount,
    // -0xC ReloadTime, -0x10 the frames a cast would get now, -0x14 the stored frames, -0x18 the current frame, -0x1C the frames
    // left, -0x20 the template. Keeps esi and edi.
    uintptr_t emitPower(Cave &cave) {
        const uintptr_t power = cave.here();
        size_t out[11];
        size_t outs = 0;

        const BYTE prologue[14] = {
            0x55, // push ebp
            0x8B, 0xEC, // mov ebp, esp
            0x83, 0xEC, 0x24, // sub esp, 0x24
            0x53, // push ebx
            0x8B, 0xD8, // mov ebx, eax: the interface
            0x8B, 0x03, // mov eax, [ebx]
            0x8B, 0x40, 0x3C // mov eax, [eax + 0x3C]: its startPowerRecharge
        };
        cave.emit(prologue);
        cave.emit8(0x3D); // cmp eax, START_POWER_RECHARGE: the other one keeps no duration
        cave.emit32(static_cast<uint32_t>(START_POWER_RECHARGE));
        out[outs++] = forwardJcc(cave, JNE);
        const BYTE gameLogic[5] = {0xA1, 0x2C, 0x41, 0xDE, 0x00}; // mov eax, [TheGameLogic]
        cave.emit(gameLogic);
        const BYTE resultTest[2] = {0x85, 0xC0}; // test eax, eax
        cave.emit(resultTest);
        out[outs++] = forwardJcc(cave, JE);
        const BYTE now[9] = {
            0x8B, 0x40, 0x40, // mov eax, [eax + 0x40]: the logic frame
            0x89, 0x45, 0xE8, // mov [ebp - 0x18], eax
            0x3B, 0x43, 0x08 // cmp eax, [ebx + 8]: the ready frame
        };
        cave.emit(now);
        out[outs++] = forwardJcc(cave, JAE); // ready: nothing to rescale
        const BYTE paused[4] = {0x83, 0x7B, 0x0C, 0x00}; // cmp dword [ebx + 0xC], 0
        cave.emit(paused);
        out[outs++] = forwardJcc(cave, JNE);
        const BYTE storedFrames[5] = {0x8B, 0x43, 0x04, 0x85, 0xC0}; // mov eax, [ebx + 4] / test eax, eax
        cave.emit(storedFrames);
        out[outs++] = forwardJcc(cave, JLE);
        const BYTE template_[12] = {
            0x89, 0x45, 0xEC, // mov [ebp - 0x14], eax
            0x8B, 0x13, // mov edx, [ebx]
            0x8B, 0xCB, // mov ecx, ebx
            0xFF, 0x52, 0x18, // call [edx + 0x18]: the template
            0x85, 0xC0 // test eax, eax
        };
        cave.emit(template_);
        out[outs++] = forwardJcc(cave, JE);
        const BYTE overrideArgument[2] = {0x8B, 0xC8}; // mov ecx, eax
        cave.emit(overrideArgument);
        cave.call(FINAL_OVERRIDE);
        cave.emit(resultTest);
        out[outs++] = forwardJcc(cave, JE);
        const BYTE sharedTimer[7] = {
            0x89, 0x45, 0xE0, // mov [ebp - 0x20], eax
            0x80, 0x78, 0x59, 0x00 // cmp byte [eax + 0x59], 0: SharedSyncedTimer
        };
        cave.emit(sharedTimer);
        out[outs++] = forwardJcc(cave, JNE);
        const BYTE reloadTime[5] = {0x8B, 0x40, 0x20, 0x85, 0xC0}; // mov eax, [eax + 0x20]: ReloadTime / test eax, eax
        cave.emit(reloadTime);
        out[outs++] = forwardJcc(cave, JLE);

        // the multiplier as a cast computes it now; getModifierMultiplier leaves the seed alone for an unmodified object
        const BYTE multiplier[29] = {
            0x89, 0x45, 0xF4, // mov [ebp - 0xC], eax
            0xC7, 0x45, 0xFC, 0x00, 0x00, 0x80, 0x3F, // mov dword [ebp - 4], 1.0f
            0xC7, 0x45, 0xF8, 0x00, 0x00, 0x80, 0x3F, // mov dword [ebp - 8], 1.0f
            0x6A, 0x01, // push 1
            0x6A, 0x00, // push 0
            0x8D, 0x45, 0xFC, // lea eax, [ebp - 4]
            0x50, // push eax
            0x6A, 0x0C, // push 0xC: RECHARGE_TIME
            0x8B, 0xCE // mov ecx, esi
        };
        cave.emit(multiplier);
        cave.call(GET_MODIFIER_MULTIPLIER);
        const BYTE player[2] = {0x8B, 0xCE}; // mov ecx, esi
        cave.emit(player);
        cave.call(GET_CONTROLLING_PLAYER);
        cave.emit(resultTest);
        const size_t noPlayer = forwardJcc(cave, JE);
        const BYTE respectsDiscount[7] = {
            0x8B, 0x4D, 0xE0, // mov ecx, [ebp - 0x20]
            0xF6, 0x41, 0x18, 0x20 // test byte [ecx + 0x18], 0x20: RESPECT_RECHARGE_TIME_DISCOUNT
        };
        cave.emit(respectsDiscount);
        const size_t noDiscount = forwardJcc(cave, JE);
        cave.emit(overrideArgument);
        cave.call(PLAYER_RECHARGE_DISCOUNT);
        const BYTE discount[9] = {
            0xD8, 0x05, 0x08, 0x19, 0xBD, 0x00, // fadd dword [0x00BD1908]: 1.0
            0xD9, 0x5D, 0xF8 // fstp dword [ebp - 8]
        };
        cave.emit(discount);

        // frames now = max(1, ftol(ReloadTime * discount * multiplier)), in the order and precision of the cast
        const uintptr_t framesNow = cave.here();
        const BYTE frames[9] = {
            0xDB, 0x45, 0xF4, // fild dword [ebp - 0xC]
            0xD8, 0x4D, 0xF8, // fmul dword [ebp - 8]
            0xD8, 0x4D, 0xFC // fmul dword [ebp - 4]
        };
        cave.emit(frames);
        cave.call(FTOL);
        const BYTE atLeastOne[9] = {
            0x85, 0xC0, // test eax, eax
            0x7F, 0x05, // jg
            0xB8, 0x01, 0x00, 0x00, 0x00 // mov eax, 1
        };
        cave.emit(atLeastOne);
        const BYTE unchanged[3] = {0x3B, 0x45, 0xEC}; // cmp eax, [ebp - 0x14]
        cave.emit(unchanged);
        out[outs++] = forwardJcc(cave, JE); // the exit nearly every power takes

        // left = max(1, ftol(left * frames now / stored frames))
        const BYTE rescale[21] = {
            0x89, 0x45, 0xF0, // mov [ebp - 0x10], eax
            0x8B, 0x43, 0x08, // mov eax, [ebx + 8]
            0x2B, 0x45, 0xE8, // sub eax, [ebp - 0x18]
            0x89, 0x45, 0xE4, // mov [ebp - 0x1C], eax
            0xDB, 0x45, 0xE4, // fild dword [ebp - 0x1C]
            0xDA, 0x4D, 0xF0, // fimul dword [ebp - 0x10]
            0xDA, 0x75, 0xEC // fidiv dword [ebp - 0x14]
        };
        cave.emit(rescale);
        cave.call(FTOL);
        cave.emit(atLeastOne);
        const BYTE store[12] = {
            0x03, 0x45, 0xE8, // add eax, [ebp - 0x18]
            0x89, 0x43, 0x08, // mov [ebx + 8], eax: the ready frame
            0x8B, 0x45, 0xF0, // mov eax, [ebp - 0x10]
            0x89, 0x43, 0x04 // mov [ebx + 4], eax: the frames
        };
        cave.emit(store);

        const uintptr_t powerOut = cave.here();
        const BYTE epilogue[5] = {
            0x8B, 0x5D, 0xD8, // mov ebx, [ebp - 0x28]
            0xC9, // leave
            0xC3 // ret
        };
        cave.emit(epilogue);

        for (size_t i = 0; i < outs; i++) {
            cave.resolveJcc(out[i], powerOut);
        }
        cave.resolveJcc(noPlayer, framesNow);
        cave.resolveJcc(noDiscount, framesNow);

        return power;
    }

    // object(esi = object): offers every module's special power to power()
    uintptr_t emitObject(Cave &cave, const uintptr_t power) {
        const uintptr_t object = cave.here();
        const BYTE modules[9] = {
            0x57, // push edi
            0x8B, 0xBE, 0x4C, 0x02, 0x00, 0x00, // mov edi, [esi + 0x24C]: the modules, null terminated
            0x85, 0xFF // test edi, edi
        };
        cave.emit(modules);
        const size_t noModules = forwardJcc(cave, JE);
        const uintptr_t loop = cave.here();
        const BYTE module[4] = {0x8B, 0x07, 0x85, 0xC0}; // mov eax, [edi] / test eax, eax
        cave.emit(module);
        const size_t lastModule = forwardJcc(cave, JE);
        const BYTE specialPower[12] = {
            0x8D, 0x48, 0x0C, // lea ecx, [eax + 0xC]
            0x8B, 0x11, // mov edx, [ecx]
            0xFF, 0x52, 0x20, // call [edx + 0x20]: its special power interface or null
            0x85, 0xC0, // test eax, eax
            0x74, 0x05 // je next
        };
        cave.emit(specialPower);
        cave.call(power);
        const BYTE next[3] = {0x83, 0xC7, 0x04}; // add edi, 4
        cave.emit(next);
        cave.jmp(loop);
        const uintptr_t out = cave.here();
        const BYTE epilogue[2] = {0x5F, 0xC3}; // pop edi / ret
        cave.emit(epilogue);
        cave.resolveJcc(noModules, out);
        cave.resolveJcc(lastModule, out);
        return object;
    }

    // sweep(): every object of the logic's list, once
    uintptr_t emitSweep(Cave &cave, const uintptr_t object) {
        const uintptr_t sweep = cave.here();
        const BYTE gameLogic[8] = {
            0x56, // push esi
            0xA1, 0x2C, 0x41, 0xDE, 0x00, // mov eax, [TheGameLogic]
            0x85, 0xC0 // test eax, eax
        };
        cave.emit(gameLogic);
        const size_t noGameLogic = forwardJcc(cave, JE);
        const BYTE first[6] = {0x8B, 0xB0, 0xAC, 0x00, 0x00, 0x00}; // mov esi, [eax + 0xAC]: the first object
        cave.emit(first);
        const uintptr_t loop = cave.here();
        const BYTE objectTest[2] = {0x85, 0xF6}; // test esi, esi
        cave.emit(objectTest);
        const size_t lastObject = forwardJcc(cave, JE);
        cave.call(object);
        const BYTE next[6] = {0x8B, 0xB6, 0x8C, 0x00, 0x00, 0x00}; // mov esi, [esi + 0x8C]: the next object
        cave.emit(next);
        cave.jmp(loop);
        const uintptr_t out = cave.here();
        const BYTE epilogue[2] = {0x5E, 0xC3}; // pop esi / ret
        cave.emit(epilogue);
        cave.resolveJcc(noGameLogic, out);
        cave.resolveJcc(lastObject, out);
        return sweep;
    }
}

bool applyCooldownModifiers() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);
    const uintptr_t power = emitPower(cave);
    const uintptr_t object = emitObject(cave, power);
    const uintptr_t sweep = emitSweep(cave, object);

    // In place of the frame gate call, with ecx TheGameLogic: the gate's al goes back to the caller untouched, and the sweep runs
    // exactly when the caller advances the frame (al set, bl clear).
    const uintptr_t tick = cave.here();
    cave.call(FRAME_GATE);
    const BYTE advances[9] = {
        0x84, 0xC0, // test al, al
        0x74, 0x0B, // je out
        0x84, 0xDB, // test bl, bl
        0x75, 0x07, // jne out
        0x60 // pushad
    };
    cave.emit(advances);
    cave.call(sweep);
    const BYTE out[2] = {0x61, 0xC3}; // popad / out: ret
    cave.emit(out);

    // In place of the partial recharge's store of the ready frame, with eax the ready frame, edi today's frames, esi the interface
    const uintptr_t partialStore = cave.here();
    const BYTE storeBoth[6] = {
        0x89, 0x46, 0x08, // mov [esi + 8], eax
        0x89, 0x7E, 0x04 // mov [esi + 4], edi
    };
    cave.emit(storeBoth);
    cave.jmp(RECHARGE_DONE);

    static PatchSet set;
    set.reset();

    // mov ecx, esi / call FRAME_GATE / test al, al / je / test bl, bl / jne / inc dword [esi + 0x40]: the frame advances
    const BYTE gateCall[5] = {0xE8, 0xC1, 0x6B, 0xFF, 0xFF};
    set.redirectCall(FRAME_GATE_CALL, gateCall, tick);
    const BYTE gateContext[2] = {0x8B, 0xCE};
    set.expect(FRAME_GATE_CALL - 2, gateContext);
    const BYTE frameAdvance[11] = {0x84, 0xC0, 0x74, 0x07, 0x84, 0xDB, 0x75, 0x07, 0xFF, 0x46, 0x40};
    set.expect(FRAME_GATE_CALL + 5, frameAdvance);
    const BYTE frameGate[14] = {0x33, 0xC0, 0x38, 0x41, 0x44, 0x74, 0x11, 0x38, 0x81, 0xA8, 0x00, 0x00, 0x00, 0x75};
    set.expect(FRAME_GATE, frameGate);

    // the logic's own walks of its object list: the head at + 0xAC, the link at Object + 0x8C
    const BYTE listHead[6] = {0x8B, 0x9E, 0xAC, 0x00, 0x00, 0x00};
    set.expect(OBJECT_LIST_WALK, listHead);
    const BYTE listNext[10] = {0x8B, 0x9B, 0x8C, 0x00, 0x00, 0x00, 0x85, 0xDB, 0x75, 0xDB};
    set.expect(OBJECT_LIST_NEXT, listNext);
    // add eax, 0xC / mov edx, [eax] / mov ecx, eax / call [edx + 0x20]: a module's special power interface
    const BYTE getSpecialPower[10] = {0x83, 0xC0, 0x0C, 0x8B, 0x10, 0x8B, 0xC8, 0xFF, 0x52, 0x20};
    set.expect(GET_SPECIAL_POWER, getSpecialPower);
    // the null terminated module array at Object + 0x24C
    const BYTE moduleWalk[24] = {
        0x56, 0x8B, 0xB1, 0x4C, 0x02, 0x00, 0x00, 0xEB, 0x16, 0xD9, 0x44, 0x24, 0x08, 0x8D, 0x48, 0x0C, 0x8B, 0x01, 0x51, 0xD9,
        0x1C, 0x24, 0xFF, 0x90
    };
    set.expect(MODULE_WALK, moduleWalk);
    const BYTE moduleWalkEnd[13] = {0xAC, 0x00, 0x00, 0x00, 0x83, 0xC6, 0x04, 0x8B, 0x06, 0x85, 0xC0, 0x75, 0xE4};
    set.expect(MODULE_WALK + 24, moduleWalkEnd);

    // startPowerRecharge: the entry, the SharedSyncedTimer test, the RECHARGE_TIME query, the discount, the formula, the stores
    const BYTE recharge[24] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7E, 0xF4, 0x83, 0x7F, 0x08, 0x00, 0x0F, 0x84, 0x4B,
        0x01, 0x00, 0x00, 0x53
    };
    set.expect(START_POWER_RECHARGE, recharge);
    const BYTE rechargeShared[16] = {0xE8, 0xCB, 0x1E, 0xDF, 0xFF, 0x80, 0x78, 0x59, 0x00, 0x74, 0x10, 0xFF, 0x75, 0xF4, 0x8B, 0x4D};
    set.expect(START_POWER_RECHARGE + 0x3B, rechargeShared);
    const BYTE rechargeQuery[24] = {
        0xF3, 0x0F, 0x10, 0x05, 0x08, 0x19, 0xBD, 0x00, 0x6A, 0x01, 0x6A, 0x00, 0x8D, 0x45, 0xF4, 0x50, 0x6A, 0x0C, 0x8B, 0xCB,
        0xF3, 0x0F, 0x11, 0x45
    };
    set.expect(START_POWER_RECHARGE + 0x56, rechargeQuery);
    const BYTE rechargeFlag[8] = {0x8B, 0x40, 0x18, 0xC1, 0xE8, 0x05, 0xA8, 0x01};
    set.expect(START_POWER_RECHARGE + 0x89, rechargeFlag);
    const BYTE rechargeDiscount[19] = {0x74, 0x11, 0x8B, 0x4D, 0xFC, 0xE8, 0x06, 0x3C, 0xE1, 0xFF, 0xD8, 0x05, 0x08, 0x19, 0xBD, 0x00, 0xD9, 0x5D, 0xF8};
    set.expect(START_POWER_RECHARGE + 0x91, rechargeDiscount);
    const BYTE rechargeFormula[24] = {
        0x8B, 0x40, 0x20, 0x85, 0xC0, 0x89, 0x45, 0xFC, 0xDB, 0x45, 0xFC, 0x7D, 0x06, 0xD8, 0x05, 0x98, 0x86, 0xBD, 0x00, 0xD8,
        0x4D, 0xF8, 0xD8, 0x4D
    };
    set.expect(START_POWER_RECHARGE + 0xB2, rechargeFormula);
    const BYTE rechargeClamp[13] = {0xF4, 0xE8, 0xA3, 0x60, 0x1A, 0x00, 0x8B, 0xF8, 0x85, 0xFF, 0x75, 0x01, 0x47};
    set.expect(START_POWER_RECHARGE + 0xCA, rechargeClamp);
    const BYTE rechargeStores[24] = {
        0xA1, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x40, 0x40, 0x03, 0xC7, 0x89, 0x46, 0x08, 0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x2B,
        0x41, 0x40, 0x89, 0x46
    };
    set.expect(START_POWER_RECHARGE + 0x144, rechargeStores);
    // the partial recharge: frames * (1 - its new percent), ftol, plus the logic frame, into eax; edi still today's frames
    const BYTE partialRecharge[24] = {
        0x85, 0xFF, 0xD9, 0xE8, 0xD8, 0x65, 0xFC, 0x89, 0x7D, 0x08, 0xDB, 0x45, 0x08, 0x7D, 0x06, 0xD8, 0x05, 0x98, 0x86, 0xBD,
        0x00, 0xDE, 0xC9, 0xE8
    };
    set.expect(PARTIAL_RECHARGE, partialRecharge);
    const BYTE partialReadyFrame[13] = {0x3D, 0x60, 0x1A, 0x00, 0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x03, 0x41, 0x40};
    set.expect(PARTIAL_RECHARGE + 24, partialReadyFrame);
    // mov [esi + 8], eax / jmp RECHARGE_DONE
    const BYTE partialStoreStock[5] = {0x89, 0x46, 0x08, 0xEB, 0x19};
    set.hook(PARTIAL_READY_FRAME_STORE, partialStoreStock, partialStore);
    // mov byte [esi + 0x18], 0
    const BYTE rechargeDone[4] = {0xC6, 0x46, 0x18, 0x00};
    set.expect(RECHARGE_DONE, rechargeDone);

    // isReady: the pause count at + 0xC and the ready frame at + 8
    const BYTE isReady[24] = {
        0x83, 0x7E, 0x0C, 0x00, 0x75, 0x12, 0xA1, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x40, 0x40, 0x3B, 0x46, 0x08, 0x72, 0x05, 0x33,
        0xC0, 0x40, 0xEB, 0x02
    };
    set.expect(IS_READY, isReady);
    // getPercentReady divides by the frames at + 4
    const BYTE percentReady[24] = {
        0x8B, 0x46, 0x04, 0xDB, 0x46, 0x04, 0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05, 0x98, 0x86, 0xBD, 0x00, 0xDE, 0xF9, 0xD8, 0x2D,
        0x08, 0x19, 0xBD, 0x00
    };
    set.expect(PERCENT_READY, percentReady);
    // the other startPowerRecharge, which stores only the ready frame at + 4
    const BYTE rechargeOther[24] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7E, 0xE0, 0x83, 0x7F, 0x08, 0x00, 0x0F, 0x84, 0x09,
        0x01, 0x00, 0x00, 0x53
    };
    set.expect(START_POWER_RECHARGE_OTHER, rechargeOther);
    const BYTE rechargeOtherStore[8] = {0x89, 0x46, 0x04, 0x5B, 0x5F, 0x5E, 0xC9, 0xC2};
    set.expect(START_POWER_RECHARGE_OTHER + 0x11C, rechargeOtherStore);
    BYTE slot[4];
    putInt(slot, static_cast<int>(START_POWER_RECHARGE));
    set.expect(START_POWER_RECHARGE_SLOT, slot);
    BYTE otherSlot[4];
    putInt(otherSlot, static_cast<int>(START_POWER_RECHARGE_OTHER));
    set.expect(START_POWER_RECHARGE_OTHER_SLOT, otherSlot);

    // the callees
    const BYTE modifierMultiplier[14] = {0xE8, 0x74, 0xFC, 0xFF, 0xFF, 0x85, 0xC0, 0x75, 0x05, 0x32, 0xC0, 0xC2, 0x10, 0x00};
    set.expect(GET_MODIFIER_MULTIPLIER, modifierMultiplier);
    const BYTE modifierHolder[12] = {0xB8, 0x2F, 0x87, 0xB8, 0x00, 0xE8, 0x40, 0x0A, 0x3B, 0x00, 0xF6, 0x05};
    set.expect(MODIFIER_HOLDER, modifierHolder);
    const BYTE controllingPlayer[18] = {0x8B, 0x89, 0x1C, 0x03, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x05, 0xE9, 0xE8, 0x46, 0x11, 0x00, 0x33, 0xC0, 0xC3};
    set.expect(GET_CONTROLLING_PLAYER, controllingPlayer);
    const BYTE finalOverride[9] = {0x8B, 0xC1, 0x8B, 0x50, 0x04, 0x85, 0xD2, 0x74, 0x0E};
    set.expect(FINAL_OVERRIDE, finalOverride);
    const BYTE playerDiscount[7] = {0xD9, 0x81, 0x18, 0x07, 0x00, 0x00, 0xC3};
    set.expect(PLAYER_RECHARGE_DISCOUNT, playerDiscount);
    const BYTE ftol[15] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x83, 0xE4, 0xF0, 0xD9, 0xC0, 0xD9, 0x54, 0x24, 0x18};
    set.expect(FTOL, ftol);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
