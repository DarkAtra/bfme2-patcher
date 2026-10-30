#include <windows.h>
#include <cstdint>

#include "life_drain.h"
#include "patch_set.h"

// Life drain on every hit
//
// Problem: a weapon with DrainLife heals its owner for nothing when its hit is delayed (DelayTime), and a damage over time
// weapon (DOTNugget) heals on its first tick only. In 2.02 that is Karsh's KarshWightTouch.
// Fix: the drain happens where each hit lands, by what that hit dealt, with the stock rules.
//
// DamageNugget::doDamage (0x0090E683) heals the source right after target->attemptDamage(info) (0x0090E6C7): multiplier
// (nugget + 0x1B8) times the damage dealt (info + 0x70), unless the amount is 0 or the source is gone or a PROJECTILE. A delayed
// hit has dealt nothing by then: Object::attemptDamage (0x00698E7D) only queues a copy, which the delayed list (0x00697EB6) lands
// later through the body damage step (0x00697E50). A DOT registers a copy of its DamageInfo with the manager (0x00911477), whose
// later ticks call attemptDamage on it directly. So the drain moves into the two calls of the body damage step (0x00698EBA,
// 0x00697EEA), which every hit goes through, and the stock drain is skipped (0x0090E6E5). The info carries the drain in the three
// padding bytes after the Bool at DamageInfo + 0x50: the input's constructor (0x00663492) and copy constructor (0x00662ADA) are
// widened to write the whole dword, so every info starts with them zero and every copy (the delayed list's, the DOT manager's)
// carries them. doDamage's attemptDamage call and the DOT registration stamp the multiplier there when the nugget drains.
//
// pySAGE stores the multiplier's top 24 bits, which changes 0.3 and the like slightly; this stores a 1-based index into a table
// of the multipliers seen, so the drain uses the exact value. Table order depends on the process's history, the values it
// decodes to do not: they are the INI's DrainLifeMultiplier values, the same on every machine, and far fewer than 255. The
// padding is not saved, so a hit pending across a save and load does not drain. Derived from pySAGE's drain-life-fix
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t INPUT_CONSTRUCTOR_STORE = 0x00663492;
    constexpr uintptr_t INPUT_COPY_MOVE = 0x00662ADA;
    constexpr uintptr_t NUGGET_ATTEMPT_CALL = 0x0090E6C7;
    constexpr uintptr_t STOCK_DRAIN = 0x0090E6CC;
    constexpr uintptr_t STOCK_DRAIN_SKIP = 0x0090E6E5;
    constexpr uintptr_t DOT_ADD_CALL = 0x00911477;
    constexpr uintptr_t ATTEMPT_DAMAGE_CALL = 0x00698EBA;
    constexpr uintptr_t DELAYED_DAMAGE_CALL = 0x00697EEA;
    // thiscall(object, info), ret 4
    constexpr uintptr_t ATTEMPT_DAMAGE = 0x00698E7D;
    // thiscall(object, info), ret 4: the body damage step
    constexpr uintptr_t DAMAGE_NOW = 0x00697E50;
    // thiscall(manager, target id, entry), ret 8
    constexpr uintptr_t DOT_ADD = 0x00821073;
    // thiscall(game logic, id), ret 4
    constexpr uintptr_t FIND_OBJECT_BY_ID = 0x00449681;
    // thiscall(object, amount, source), ret 8
    constexpr uintptr_t ATTEMPT_HEALING = 0x00690532;

    constexpr uint32_t MAX_MULTIPLIERS = 255;
    constexpr BYTE JP = 0x8A;

    // the DrainLifeMultiplier bits, entry n - 1 for index n
    uint32_t multipliers[MAX_MULTIPLIERS];
    uint32_t multiplierCount;

    size_t forwardJcc(Cave &cave, const BYTE condition) {
        const size_t offset = cave.size;
        cave.jcc(condition, 0);
        return offset;
    }

    uint32_t address(const void *p) {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    }

    // With esi the nugget and the DamageInfo at [esp + infoArgument] on entry: when the nugget drains, writes the index of its
    // multiplier into DamageInfo + 0x51..0x53, then jumps to `target`. Keeps every register but eax.
    void emitStamp(Cave &cave, const BYTE infoArgument, const uintptr_t target) {
        const BYTE drains[7] = {0x80, 0xBE, 0xB4, 0x01, 0x00, 0x00, 0x00}; // cmp byte [esi + 0x1B4], 0: DrainLife
        cave.emit(drains);
        const size_t noDrain = forwardJcc(cave, JE);
        const BYTE multiplier[10] = {
            0x51, 0x52, // push ecx / push edx
            0x8B, 0x86, 0xB8, 0x01, 0x00, 0x00, // mov eax, [esi + 0x1B8]: DrainLifeMultiplier
            0x33, 0xC9 // xor ecx, ecx
        };
        cave.emit(multiplier);
        const uintptr_t find = cave.here();
        cave.emit8(0x3B); // cmp ecx, [multiplierCount]
        cave.emit8(0x0D);
        cave.emit32(address(&multiplierCount));
        const size_t notFound = forwardJcc(cave, JAE);
        cave.emit8(0x3B); // cmp eax, [multipliers + ecx * 4]
        cave.emit8(0x04);
        cave.emit8(0x8D);
        cave.emit32(address(multipliers));
        const size_t found = forwardJcc(cave, JE);
        cave.emit8(0x41); // inc ecx
        cave.jmp(find);

        const uintptr_t append = cave.here();
        cave.emit8(0x81); // cmp ecx, MAX_MULTIPLIERS
        cave.emit8(0xF9);
        cave.emit32(MAX_MULTIPLIERS);
        const size_t full = forwardJcc(cave, JAE);
        cave.emit8(0x89); // mov [multipliers + ecx * 4], eax
        cave.emit8(0x04);
        cave.emit8(0x8D);
        cave.emit32(address(multipliers));
        cave.increment(address(&multiplierCount));

        const uintptr_t stamp = cave.here();
        const BYTE index[16] = {
            0x41, // inc ecx: the index is 1-based, 0 means no drain
            0xC1, 0xE1, 0x08, // shl ecx, 8
            0x8B, 0x54, 0x24, static_cast<BYTE>(infoArgument + 8), // mov edx, [esp + infoArgument + 8]: the info
            0x8A, 0x4A, 0x50, // mov cl, [edx + 0x50]: keep the Bool
            0x89, 0x4A, 0x50, // mov [edx + 0x50], ecx
            0x5A, 0x59 // pop edx / pop ecx
        };
        cave.emit(index);
        const size_t stamped = cave.size;
        cave.jmp(0);
        const uintptr_t unstamped = cave.here();
        const BYTE restore[2] = {0x5A, 0x59}; // pop edx / pop ecx
        cave.emit(restore);
        const uintptr_t done = cave.here();
        cave.jmp(target);

        cave.resolveJcc(noDrain, done);
        cave.resolveJcc(notFound, append);
        cave.resolveJcc(found, stamp);
        cave.resolveJcc(full, unstamped);
        cave.resolveJmp(stamped, done);
    }
}

bool applyLifeDrain() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);

    // stands in for doDamage's target->attemptDamage(info), esi the nugget
    const uintptr_t stampAttempt = cave.here();
    emitStamp(cave, 4, ATTEMPT_DAMAGE);

    // stands in for DOTNugget::doDamage's manager->add(target id, entry), the entry starting with its DamageInfo, esi the nugget
    const uintptr_t stampDot = cave.here();
    emitStamp(cave, 8, DOT_ADD);

    // Stands in for the body damage step: ecx the target, [esp + 4] the info, ret 4. An info without a drain goes straight to the
    // stock step. Otherwise the damage dealt is cleared first (a DOT reuses one info for every tick and the step may deal nothing),
    // and after the step the source heals by multiplier x damage dealt, as the stock drain does: not for DAMAGE_HEALING (7), which
    // doDamage never sends here, not for an amount of 0, not when the source is gone or a PROJECTILE.
    const uintptr_t damageNow = cave.here();
    const BYTE drainIndex[13] = {
        0x56, // push esi
        0x8B, 0x74, 0x24, 0x08, // mov esi, [esp + 8]: the info
        0x8B, 0x46, 0x50, // mov eax, [esi + 0x50]
        0xC1, 0xE8, 0x08, // shr eax, 8: the multiplier's index
        0x85, 0xC0 // test eax, eax
    };
    cave.emit(drainIndex);
    const size_t noDrain = forwardJcc(cave, JE);
    const BYTE damageStep[6] = {
        0x83, 0x66, 0x70, 0x00, // and dword [esi + 0x70], 0: the damage dealt
        0x50, // push eax
        0x56 // push esi
    };
    cave.emit(damageStep);
    cave.call(DAMAGE_NOW);
    const BYTE healing[5] = {
        0x58, // pop eax
        0x83, 0x7E, 0x10, 0x07 // cmp dword [esi + 0x10], 7: DAMAGE_HEALING
    };
    cave.emit(healing);
    const size_t heals = forwardJcc(cave, JE);
    const BYTE amount[4] = {0xF3, 0x0F, 0x10, 0x04}; // movss xmm0, [multipliers - 4 + eax * 4]
    cave.emit(amount);
    cave.emit8(0x85);
    cave.emit32(address(multipliers) - 4);
    const BYTE dealt[11] = {
        0xF3, 0x0F, 0x59, 0x46, 0x70, // mulss xmm0, [esi + 0x70]
        0x0F, 0x57, 0xC9, // xorps xmm1, xmm1
        0x0F, 0x2E, 0xC1 // ucomiss xmm0, xmm1
    };
    cave.emit(dealt);
    const size_t unordered = forwardJcc(cave, JP); // the stock drain only skips an amount equal to 0
    const size_t zero = forwardJcc(cave, JE);
    cave.resolveJcc(unordered, cave.here());
    const BYTE source[17] = {
        0x83, 0xEC, 0x04, // sub esp, 4
        0xF3, 0x0F, 0x11, 0x04, 0x24, // movss [esp], xmm0: the amount, kept across the lookup
        0xFF, 0x76, 0x08, // push [esi + 8]: the source's id
        0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00 // mov ecx, [0x00DE412C]: TheGameLogic
    };
    cave.emit(source);
    cave.call(FIND_OBJECT_BY_ID);
    const BYTE sourceFound[2] = {0x85, 0xC0}; // test eax, eax
    cave.emit(sourceFound);
    const size_t noSource = forwardJcc(cave, JE);
    const BYTE projectile[10] = {
        0x8B, 0x48, 0x04, // mov ecx, [eax + 4]: the template
        0xF6, 0x81, 0x0B, 0x01, 0x00, 0x00, 0x02 // test byte [ecx + 0x10B], 2: KindOf PROJECTILE
    };
    cave.emit(projectile);
    const size_t isProjectile = forwardJcc(cave, JNE);
    const BYTE heal[17] = {
        0xF3, 0x0F, 0x10, 0x04, 0x24, // movss xmm0, [esp]
        0x6A, 0x00, // push 0: no healer
        0x83, 0xEC, 0x04, // sub esp, 4
        0xF3, 0x0F, 0x11, 0x04, 0x24, // movss [esp], xmm0: the amount
        0x8B, 0xC8 // mov ecx, eax
    };
    cave.emit(heal);
    cave.call(ATTEMPT_HEALING);
    const uintptr_t drop = cave.here();
    const BYTE dropAmount[3] = {0x83, 0xC4, 0x04}; // add esp, 4
    cave.emit(dropAmount);
    const uintptr_t done = cave.here();
    const BYTE out[4] = {
        0x5E, // pop esi
        0xC2, 0x04, 0x00 // ret 4
    };
    cave.emit(out);
    const uintptr_t plain = cave.here();
    cave.emit8(0x5E); // pop esi
    cave.jmp(DAMAGE_NOW);

    cave.resolveJcc(noDrain, plain);
    cave.resolveJcc(heals, done);
    cave.resolveJcc(zero, done);
    cave.resolveJcc(noSource, drop);
    cave.resolveJcc(isProjectile, drop);

    static PatchSet set;
    set.reset();

    // mov byte [eax + 0x4C], cl (ecx is zero) -> mov dword [eax + 0x4C], ecx
    const BYTE constructorStore[3] = {0x88, 0x48, 0x4C};
    const BYTE constructorStoreDword[3] = {0x89, 0x48, 0x4C};
    set.replace(INPUT_CONSTRUCTOR_STORE, constructorStore, constructorStoreDword);
    // xorps xmm0, xmm0 / movss xmm1, [1.0f] / mov eax, ecx / xor ecx, ecx / mov dword [eax], the input's vtable
    const BYTE constructorHead[19] = {
        0x0F, 0x57, 0xC0, 0xF3, 0x0F, 0x10, 0x0D, 0x08, 0x19, 0xBD, 0x00, 0x8B, 0xC1, 0x33, 0xC9, 0xC7, 0x00, 0xA8, 0xF3
    };
    set.expect(INPUT_CONSTRUCTOR_STORE - 0x76, constructorHead);
    // mov dl, [ecx + 0x4C] / mov [eax + 0x4C], dl -> mov edx, [ecx + 0x4C] / mov [eax + 0x4C], edx
    const BYTE copyMove[6] = {0x8A, 0x51, 0x4C, 0x88, 0x50, 0x4C};
    const BYTE copyMoveDword[6] = {0x8B, 0x51, 0x4C, 0x89, 0x50, 0x4C};
    set.replace(INPUT_COPY_MOVE, copyMove, copyMoveDword);
    // mov eax, ecx / mov ecx, [esp + 4] / mov dword [eax], the input's vtable
    const BYTE copyHead[12] = {0x8B, 0xC1, 0x8B, 0x4C, 0x24, 0x04, 0xC7, 0x00, 0xA8, 0xF3, 0xC0, 0x00};
    set.expect(INPUT_COPY_MOVE - 0x7E, copyHead);

    // lea eax, [ebp - 0x7C] / push eax / mov ecx, ebx
    const BYTE nuggetArguments[6] = {0x8D, 0x45, 0x84, 0x50, 0x8B, 0xCB};
    set.expect(NUGGET_ATTEMPT_CALL - 6, nuggetArguments);
    const BYTE nuggetAttemptCall[5] = {0xE8, 0xB1, 0xA7, 0xD8, 0xFF};
    set.redirectCall(NUGGET_ATTEMPT_CALL, nuggetAttemptCall, stampAttempt);
    // cmp byte [esi + 0x1B4], 0 / movss xmm0, [esi + 0x1B8] / mulss xmm0, [ebp - 0xC] / movss [ebp + 0x10], xmm0
    const BYTE stockDrainTest[7] = {0x80, 0xBE, 0xB4, 0x01, 0x00, 0x00, 0x00};
    set.expect(STOCK_DRAIN, stockDrainTest);
    const BYTE stockDrainAmount[18] = {
        0xF3, 0x0F, 0x10, 0x86, 0xB8, 0x01, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x45, 0xF4, 0xF3, 0x0F, 0x11, 0x45, 0x10
    };
    static_assert(sizeof(stockDrainTest) + sizeof(stockDrainAmount) == STOCK_DRAIN_SKIP - STOCK_DRAIN, "the drain test ends at the je");
    set.expect(STOCK_DRAIN + sizeof(stockDrainTest), stockDrainAmount);
    // je done -> jmp done
    const BYTE drainSkip[2] = {0x74, 0x3B};
    const BYTE drainSkipAlways[2] = {0xEB, 0x3B};
    set.replace(STOCK_DRAIN_SKIP, drainSkip, drainSkipAlways);
    // ucomiss xmm0, [0.0f] / lahf / test ah, 0x44 / jnp / push [edi + 8] / mov ecx, [TheGameLogic] / call findObjectByID
    const BYTE stockDrainSource[24] = {
        0x0F, 0x2E, 0x05, 0x94, 0xB5, 0xC1, 0x00, 0x9F, 0xF6, 0xC4, 0x44, 0x7B, 0x2E, 0xFF, 0x77, 0x08, 0x8B, 0x0D, 0x2C, 0x41,
        0xDE, 0x00, 0xE8, 0x7F
    };
    set.expect(STOCK_DRAIN_SKIP + 2, stockDrainSource);
    // test eax, eax / je / mov ecx, [eax + 4] / test byte [ecx + 0x10B], 2 / jne / fld [ebp + 0x10] / push 0 / push ecx
    const BYTE stockDrainHeal[20] = {
        0x85, 0xC0, 0x74, 0x1C, 0x8B, 0x48, 0x04, 0xF6, 0x81, 0x0B, 0x01, 0x00, 0x00, 0x02, 0x75, 0x10, 0xD9, 0x45, 0x10, 0x6A
    };
    set.expect(STOCK_DRAIN_SKIP + 0x1D, stockDrainHeal);
    // ... / mov ecx, eax / fstp [esp] / call attemptHealing
    const BYTE stockDrainHealCall[12] = {0x00, 0x51, 0x8B, 0xC8, 0xD9, 0x1C, 0x24, 0xE8, 0x10, 0x1E, 0xD8, 0xFF};
    set.expect(STOCK_DRAIN_SKIP + 0x31, stockDrainHealCall);

    // lea eax, [ebp - 0x1C]: the entry / push eax / push [edi + 0x74]: the target's id / add edx, ecx / mov ecx, ebx
    const BYTE dotArguments[12] = {0x8D, 0x45, 0xE4, 0x50, 0xFF, 0x77, 0x74, 0x03, 0xD1, 0x8B, 0xCB, 0x89};
    set.expect(DOT_ADD_CALL - 14, dotArguments);
    // mov edi, [ebp + 0x78] / mov esi, ecx: the nugget, in DOTNugget::doDamage's prologue
    const BYTE dotNugget[5] = {0x8B, 0x7D, 0x78, 0x8B, 0xF1};
    set.expect(DOT_ADD_CALL - 0x55, dotNugget);
    const BYTE dotAddCall[5] = {0xE8, 0xF7, 0xFB, 0xF0, 0xFF};
    set.redirectCall(DOT_ADD_CALL, dotAddCall, stampDot);

    // movss xmm0, [eax + 0x28]: the delay / comiss xmm0, [0.0f] / push eax / jbe now / ... / now: mov ecx, esi
    const BYTE attemptDamageDelay[15] = {0xF3, 0x0F, 0x10, 0x40, 0x28, 0x0F, 0x2F, 0x05, 0x94, 0xB5, 0xC1, 0x00, 0x50, 0x76, 0x0D};
    set.expect(ATTEMPT_DAMAGE_CALL - 0x1E, attemptDamageDelay);
    const BYTE attemptDamageCall[5] = {0xE8, 0x91, 0xEF, 0xFF, 0xFF};
    set.redirectCall(ATTEMPT_DAMAGE_CALL, attemptDamageCall, damageNow);
    const BYTE attemptDamageEnd[4] = {0x5E, 0xC2, 0x04, 0x00}; // pop esi / ret 4
    set.expect(ATTEMPT_DAMAGE_CALL + 5, attemptDamageEnd);

    // jbe: still waiting / push eax: the info / mov ecx, ebx: the object
    const BYTE delayedArguments[5] = {0x76, 0x1D, 0x50, 0x8B, 0xCB};
    set.expect(DELAYED_DAMAGE_CALL - 5, delayedArguments);
    const BYTE delayedDamageCall[5] = {0xE8, 0x61, 0xFF, 0xFF, 0xFF};
    set.redirectCall(DELAYED_DAMAGE_CALL, delayedDamageCall, damageNow);
    // xorps xmm1, xmm1 at the loop's head, so the zero survives the step
    const BYTE delayedLoopHead[3] = {0x0F, 0x57, 0xC9};
    set.expect(DELAYED_DAMAGE_CALL - 0x20, delayedLoopHead);

    // push esi / mov esi, ecx / test byte [esi + 0x458], 1
    const BYTE damageNowHead[10] = {0x56, 0x8B, 0xF1, 0xF6, 0x86, 0x58, 0x04, 0x00, 0x00, 0x01};
    set.expect(DAMAGE_NOW, damageNowHead);
    // push esi / push 0x48 / mov esi, ecx
    const BYTE attemptDamageHead[5] = {0x56, 0x6A, 0x48, 0x8B, 0xF1};
    set.expect(ATTEMPT_DAMAGE, attemptDamageHead);
    // push ebp / mov ebp, esp / push ebx / push esi / push edi / mov edi, ecx
    const BYTE dotAddHead[8] = {0x55, 0x8B, 0xEC, 0x53, 0x56, 0x57, 0x8B, 0xF9};
    set.expect(DOT_ADD, dotAddHead);
    // cmp dword [esp + 4], 0 / je / lea
    const BYTE findObject[8] = {0x83, 0x7C, 0x24, 0x04, 0x00, 0x74, 0x14, 0x8D};
    set.expect(FIND_OBJECT_BY_ID, findObject);
    // push ebp / mov ebp, esp / sub esp, 0x7C / push esi / mov esi, [ecx + 0x25C]
    const BYTE attemptHealing[13] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x7C, 0x56, 0x8B, 0xB1, 0x5C, 0x02, 0x00, 0x00};
    set.expect(ATTEMPT_HEALING, attemptHealing);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
