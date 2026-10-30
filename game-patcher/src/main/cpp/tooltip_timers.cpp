#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "tooltip_timers.h"

// Tooltip timers
//
// Problem: a button's tooltip does not say how long its ability recharges, its unit takes to build or its upgrade to research.
// Fix: the description gets a last line with that time in whole seconds, formatted from the mod's strings TOOLTIP:Cooldown,
// TOOLTIP:CooldownRemaining, TOOLTIP:BuildTime and TOOLTIP:ResearchTime (one %d each). A string the mod does not define means
// no line, so without the strings nothing changes.
//
// The button tooltip builder (0x00807A81) keeps the description at [ebp - 0x18], the selected object (or 0) at [ebp - 0x1C] and
// its player (or the local player) at [ebp - 0x20]; every case joins at 0x008086AE, where the description is final. The button
// is only in esi + 0xC early on (three cases reuse esi), so the prologue's load of it (0x00807AF9) also stores it here. At the
// join the line is added:
// * an ability (button + 0x44): the cooldown a cast would get now, startPowerRecharge's formula (0x00896E87) with today's
//   RECHARGE_TIME multiplier and player discount; while it recharges the frames left to the module's ready frame instead (the
//   two module layouts, told apart by their startPowerRecharge, 0x00896E31 and 0x00991500). The owner is the selected object if
//   it has the power, otherwise the player's spellbook (palantir buttons). Paused recharges, SharedSyncedTimer powers and
//   modules of neither layout show the full cooldown; powers without a ReloadTime show nothing;
// * an upgrade (button + 0x24) not yet researched (by the object for Type = OBJECT, by the player otherwise): its research
//   time, UpgradeTemplate::calcTimeToBuild (0x0066F1A8), the function the production queue uses;
// * a unit or structure (the button's ThingTemplate, not for heroes or the two commands the builder shows no cost for): its
//   build time, ThingTemplate::calcTimeToBuild (0x0073C39E), the queue's function too.
// Frames become seconds through the logic frame rate (0x00D9F608, 5); under one second shows nothing. The tooltip is built once
// per hover, so the remaining cooldown is the time left when it appeared, and a unit already queued shows its full build time.
// Only the client's tooltip changes. Derived from pySAGE's description-timers (https://github.com/ClementJ18/pySAGE); pySAGE
// formats the line with the wchar_t overload of UnicodeString::format (0x00ADF750), this uses the one the game uses for fetched
// strings (0x00ADF7E0).

using namespace patching;

namespace {

    constexpr uintptr_t BUTTON_CAPTURE = 0x00807AF9;
    constexpr uintptr_t BUTTON_CAPTURE_RESUME = 0x00807AFF;
    constexpr uintptr_t DESCRIPTION_FINAL = 0x008086AE;
    constexpr uintptr_t DESCRIPTION_FINAL_RESUME = 0x008086B4;
    constexpr uintptr_t COST_COMMANDS = 0x00807F0C;
    constexpr uintptr_t HERO_TEST = 0x0080774E;
    constexpr uintptr_t STOCK_LINE = 0x008083AF;
    constexpr uintptr_t FRAME_RATE_SETUP = 0x00644FA6;
    constexpr uintptr_t RECHARGE_FORMULA = 0x00896E87;
    constexpr uintptr_t READY_TEST = 0x00896CD4;
    constexpr uintptr_t READY_TEST_ALT = 0x0099139E;

    // thiscall(button): its ThingTemplate
    constexpr uintptr_t GET_THING_TEMPLATE = 0x0075D1DC;
    // thiscall(object, power template), ret 4: the module's special power interface or null
    constexpr uintptr_t GET_SPECIAL_POWER_MODULE = 0x0068C26D;
    // thiscall(object, type, out, 0, 1), ret 0x10
    constexpr uintptr_t GET_MODIFIER_MULTIPLIER = 0x0068C82D;
    // thiscall(object)
    constexpr uintptr_t CONTROLLING_PLAYER = 0x0068B678;
    // thiscall(template): the INI override in effect
    constexpr uintptr_t FINAL_OVERRIDE = 0x00688D3C;
    // thiscall(player): Player + 0x718 on the x87 stack
    constexpr uintptr_t PLAYER_RECHARGE_DISCOUNT = 0x006AAAD2;
    // thiscall(player, upgrade), ret 4
    constexpr uintptr_t PLAYER_HAS_UPGRADE = 0x006AC2AF;
    // thiscall(object, upgrade), ret 4
    constexpr uintptr_t OBJECT_HAS_UPGRADE = 0x00691421;
    // thiscall(template, player, object, -1), ret 0xC: frames
    constexpr uintptr_t THING_BUILD_TIME = 0x0073C39E;
    // thiscall(upgrade, player, object), ret 8: frames
    constexpr uintptr_t UPGRADE_BUILD_TIME = 0x0066F1A8;
    // slot + 0x3C of the special power interface, which tells the two layouts apart
    constexpr uintptr_t START_POWER_RECHARGE = 0x00896E31;
    constexpr uintptr_t START_POWER_RECHARGE_ALT = 0x00991500;
    // cdecl(object, context): KINDOF SPELL_BOOK of the player context[0] into context[1]
    constexpr uintptr_t SPELL_BOOK_FINDER = 0x006AAE3C;
    // thiscall(player, callback, context), ret 8
    constexpr uintptr_t FOR_EACH_TEAM_OBJECT = 0x006ABABD;
    // cdecl(destination, format string, ...): replaces the destination
    constexpr uintptr_t UNICODE_STRING_FORMAT = 0x00ADF7E0;
    // thiscall(string, other), ret 4
    constexpr uintptr_t UNICODE_STRING_APPEND = 0x004065FE;
    // thiscall(string, wide characters), ret 4
    constexpr uintptr_t UNICODE_STRING_APPEND_WIDE = 0x00405183;
    // thiscall(string)
    constexpr uintptr_t UNICODE_STRING_RELEASE = 0x004367B0;
    constexpr uintptr_t FTOL = 0x00A3CFA4;

    constexpr uintptr_t THE_GAME_LOGIC = 0x00DE412C;
    constexpr uintptr_t THE_GAME_TEXT = 0x00DE4B04;
    constexpr uintptr_t LOGIC_FRAMES_PER_SECOND = 0x00D9F608;
    constexpr uintptr_t WIDE_NEWLINE = 0x00BDBC40;
    constexpr uint32_t GAME_TEXT_FETCH = 0x44;
    constexpr uint32_t RECHARGE_TIME = 0xC;

    typedef void *(__fastcall *GetPointer)(const void *self, void *);
    typedef void *(__fastcall *GetSpecialPowerModule)(void *object, void *, void *power);
    typedef bool (__fastcall *GetModifierMultiplier)(void *object, void *, uint32_t type, float *multiplier, int, int);
    typedef float (__fastcall *GetFloat)(void *self, void *);
    typedef bool (__fastcall *HasUpgrade)(void *owner, void *, void *upgrade);
    typedef int (__fastcall *ThingBuildTime)(void *thing, void *, void *player, void *object, int override);
    typedef int (__fastcall *UpgradeBuildTime)(void *upgrade, void *, void *player, void *object);
    typedef bool (__fastcall *ForEachTeamObject)(void *player, void *, uintptr_t callback, void *context);
    typedef void *(__fastcall *Fetch)(void *gameText, void *, const char *label, bool *exists);
    typedef void (__cdecl *Format)(void **destination, void *format, int value);
    typedef void (__fastcall *Append)(void **string, void *, const void *other);
    typedef void (__fastcall *Release)(void **string, void *);

    // the button whose tooltip is being built, from the builder's prologue; the builder runs on the main thread and only on hover
    const uint8_t *capturedButton;

    template<typename T>
    T field(const void *base, const uint32_t offset) {
        return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
    }

    // startPowerRecharge's arithmetic on the x87 stack, so it rounds under the game's precision control like the cast: the
    // discount (0x00896ECC) and the frames (0x00896EEB)
    float discountFactor(float playerDiscount) {
        const float one = 1.0f;
        float factor;
        __asm {
            fld playerDiscount
            fadd one
            fstp factor
        }
        return factor;
    }

    int rechargeFrames(int reload, float discount, float multiplier) {
        const uintptr_t ftol = FTOL;
        int frames;
        __asm {
            fild reload
            fmul discount
            fmul multiplier
            call ftol
            mov frames, eax
        }
        return frames;
    }

    void addLine(uint8_t *frame, const char *label, const int frames) {
        const int rate = *reinterpret_cast<const int *>(LOGIC_FRAMES_PER_SECOND);
        void *gameText = *reinterpret_cast<void **>(THE_GAME_TEXT);
        if (rate <= 0 || frames < rate || gameText == nullptr) {
            return;
        }
        bool exists = false;
        void *format = reinterpret_cast<Fetch>(field<uintptr_t>(field<void *>(gameText, 0), GAME_TEXT_FETCH))(gameText, nullptr, label, &exists);
        if (!exists) {
            return;
        }
        auto **description = reinterpret_cast<void **>(frame - 0x18);
        if (*description != nullptr && field<uint16_t>(*description, 4) != 0) {
            reinterpret_cast<Append>(UNICODE_STRING_APPEND_WIDE)(description, nullptr, reinterpret_cast<const void *>(WIDE_NEWLINE));
        }
        void *line = nullptr;
        reinterpret_cast<Format>(UNICODE_STRING_FORMAT)(&line, format, frames / rate);
        reinterpret_cast<Append>(UNICODE_STRING_APPEND)(description, nullptr, &line);
        reinterpret_cast<Release>(UNICODE_STRING_RELEASE)(&line, nullptr);
    }

    // palantir buttons describe the selection, which does not own their power: the player's spellbook does
    void *findPowerModule(uint8_t *frame, void *power, void *&owner) {
        owner = field<void *>(frame, -0x1C);
        if (owner != nullptr) {
            void *module = reinterpret_cast<GetSpecialPowerModule>(GET_SPECIAL_POWER_MODULE)(owner, nullptr, power);
            if (module != nullptr) {
                return module;
            }
        }
        void *player = field<void *>(frame, -0x20);
        if (player == nullptr) {
            return nullptr;
        }
        void *context[2] = {player, nullptr};
        reinterpret_cast<ForEachTeamObject>(FOR_EACH_TEAM_OBJECT)(player, nullptr, SPELL_BOOK_FINDER, context);
        if (context[1] == nullptr) {
            return nullptr;
        }
        void *module = reinterpret_cast<GetSpecialPowerModule>(GET_SPECIAL_POWER_MODULE)(context[1], nullptr, power);
        if (module != nullptr) {
            owner = context[1];
        }
        return module;
    }

    void addCooldown(uint8_t *frame, void *power) {
        void *owner;
        const void *module = findPowerModule(frame, power, owner);

        float multiplier = 1.0f;
        float discount = 1.0f;
        void *player = field<void *>(frame, -0x20);
        if (owner != nullptr) {
            reinterpret_cast<GetModifierMultiplier>(GET_MODIFIER_MULTIPLIER)(owner, nullptr, RECHARGE_TIME, &multiplier, 0, 1);
            player = reinterpret_cast<GetPointer>(CONTROLLING_PLAYER)(owner, nullptr);
        }
        const void *final = reinterpret_cast<GetPointer>(FINAL_OVERRIDE)(power, nullptr);
        if ((field<uint32_t>(final, 0x18) >> 5 & 1) != 0 && player != nullptr) {
            discount = discountFactor(reinterpret_cast<GetFloat>(PLAYER_RECHARGE_DISCOUNT)(player, nullptr));
        }
        const int reload = field<int>(final, 0x20);
        if (reload <= 0) {
            return;
        }
        const int frames = rechargeFrames(reload, discount, multiplier);
        if (frames <= 0) {
            return;
        }

        if (module != nullptr) {
            const uintptr_t startRecharge = field<uintptr_t>(field<void *>(module, 0), 0x3C);
            uint32_t pauseOffset = 0;
            uint32_t readyOffset = 0;
            if (startRecharge == START_POWER_RECHARGE) {
                pauseOffset = 0xC;
                readyOffset = 0x8;
            } else if (startRecharge == START_POWER_RECHARGE_ALT) {
                pauseOffset = 0x8;
                readyOffset = 0x4;
            }
            const void *logic = *reinterpret_cast<void **>(THE_GAME_LOGIC);
            if (readyOffset != 0 && field<uint32_t>(module, pauseOffset) == 0 && logic != nullptr) {
                const int remaining = field<int>(module, readyOffset) - field<int>(logic, 0x40);
                if (remaining > 0) {
                    addLine(frame, "TOOLTIP:CooldownRemaining", remaining);
                    return;
                }
            }
        }
        addLine(frame, "TOOLTIP:Cooldown", frames);
    }

    void addResearchTime(uint8_t *frame, void *upgrade) {
        void *object = field<void *>(frame, -0x1C);
        void *player = field<void *>(frame, -0x20);
        if (player == nullptr) {
            return;
        }
        if (field<int>(upgrade, 4) == 1) {
            if (object != nullptr && reinterpret_cast<HasUpgrade>(OBJECT_HAS_UPGRADE)(object, nullptr, upgrade)) {
                return;
            }
        } else if (reinterpret_cast<HasUpgrade>(PLAYER_HAS_UPGRADE)(player, nullptr, upgrade)) {
            return;
        }
        const int frames = reinterpret_cast<UpgradeBuildTime>(UPGRADE_BUILD_TIME)(upgrade, nullptr, player, object);
        if (frames > 0) {
            addLine(frame, "TOOLTIP:ResearchTime", frames);
        }
    }

    void addBuildTime(uint8_t *frame, const uint8_t *button) {
        const int command = field<int>(button, 0x14);
        if (command == 0x19 || command == 0x34) {
            return;
        }
        void *thing = reinterpret_cast<GetPointer>(GET_THING_TEMPLATE)(button, nullptr);
        if (thing == nullptr || (field<uint8_t>(thing, 0x11F) & 0x40) != 0) {
            return;
        }
        const int frames = reinterpret_cast<ThingBuildTime>(THING_BUILD_TIME)(thing, nullptr, field<void *>(frame, -0x20), field<void *>(frame, -0x1C), -1);
        if (frames > 0) {
            addLine(frame, "TOOLTIP:BuildTime", frames);
        }
    }

    void __cdecl addTimer(uint8_t *frame) {
        const uint8_t *button = capturedButton;
        if (button == nullptr) {
            return;
        }
        if (void *power = field<void *>(button, 0x44)) {
            addCooldown(frame, power);
        } else if (void *upgrade = field<void *>(button, 0x24)) {
            addResearchTime(frame, upgrade);
        } else {
            addBuildTime(frame, button);
        }
    }
}

bool applyTooltipTimers() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);
    const uintptr_t capture = cave.here();
    const BYTE loadButton[6] = {
        0x89, 0x7D, 0xC4, // mov [ebp - 0x3C], edi
        0x8B, 0x4E, 0x0C // mov ecx, [esi + 0xC]: the button
    };
    cave.emit(loadButton);
    cave.emit8(0x89); // mov [capturedButton], ecx
    cave.emit8(0x0D);
    cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&capturedButton)));
    cave.jmp(BUTTON_CAPTURE_RESUME);

    const uintptr_t join = cave.here();
    const BYTE saveRegisters[2] = {
        0x60, // pushad
        0x55 // push ebp
    };
    cave.emit(saveRegisters);
    cave.call(reinterpret_cast<uintptr_t>(&addTimer));
    const BYTE restoreRegisters[10] = {
        0x83, 0xC4, 0x04, // add esp, 4
        0x61, // popad
        0x8B, 0x0D, 0x70, 0x4A, 0xDE, 0x00 // mov ecx, [0x00DE4A70]
    };
    cave.emit(restoreRegisters);
    cave.jmp(DESCRIPTION_FINAL_RESUME);

    static PatchSet set;
    set.reset();

    // mov [ebp - 0x3C], edi / mov ecx, [esi + 0xC], unconditional from the entry; the null test follows
    const BYTE buttonLoad[6] = {0x89, 0x7D, 0xC4, 0x8B, 0x4E, 0x0C};
    set.hook(BUTTON_CAPTURE, buttonLoad, capture);
    // push ...: the empty string / lea ecx, [ebp - 0x34] / mov [ebp - 0x20], eax: the player / call
    const BYTE playerSlot[16] = {0x68, 0xF0, 0x25, 0xDF, 0x00, 0x8D, 0x4D, 0xCC, 0x89, 0x45, 0xE0, 0xE8, 0x97, 0xEE, 0xC2, 0xFF};
    set.expect(BUTTON_CAPTURE - 0x10, playerSlot);
    // mov eax, [eax + 0xFC] / mov [ebp - 0x1C], eax: the selected object / jmp / mov [ebp - 0x1C], edi
    const BYTE objectSlot[14] = {0x8B, 0x80, 0xFC, 0x00, 0x00, 0x00, 0x89, 0x45, 0xE4, 0xEB, 0x03, 0x89, 0x7D, 0xE4};
    set.expect(BUTTON_CAPTURE - 0x39, objectSlot);
    // cmp ecx, edi / ... / je 0x008086AB: a null button skips to the join, before the hook
    const BYTE nullButton[20] = {
        0x3B, 0xCF, 0xC6, 0x45, 0xFC, 0x06, 0xC6, 0x45, 0xF1, 0x01, 0xC6, 0x45, 0xF0, 0x00, 0x0F, 0x84, 0x98, 0x0B, 0x00, 0x00
    };
    set.expect(BUTTON_CAPTURE_RESUME, nullButton);

    // xor edi, edi / pop ebx / mov [ebp - 0x48], edi: the join
    const BYTE joinHead[6] = {0x33, 0xFF, 0x5B, 0x89, 0x7D, 0xB8};
    set.expect(DESCRIPTION_FINAL - 6, joinHead);
    const BYTE finalLoad[6] = {0x8B, 0x0D, 0x70, 0x4A, 0xDE, 0x00};
    set.hook(DESCRIPTION_FINAL, finalLoad, join);
    // cmp ecx, edi / mov byte [ebp - 4], 0x1D
    const BYTE finalResume[6] = {0x3B, 0xCF, 0xC6, 0x45, 0xFC, 0x1D};
    set.expect(DESCRIPTION_FINAL_RESUME, finalResume);

    // mov eax, [esi + 0xC] / mov eax, [eax + 0x14] / cmp eax, 0x19 / je / cmp eax, 0x34 / je: no cost line for these commands
    const BYTE costCommands[24] = {
        0x8B, 0x46, 0x0C, 0x8B, 0x40, 0x14, 0x83, 0xF8, 0x19, 0x0F, 0x84, 0x29, 0x02, 0x00, 0x00, 0x83, 0xF8, 0x34, 0x0F, 0x84,
        0x20, 0x02, 0x00, 0x00
    };
    set.expect(COST_COMMANDS, costCommands);
    // the hero test: test byte [template + 0x11F], 0x40
    const BYTE heroTest[11] = {0x8B, 0x44, 0x24, 0x0C, 0xF6, 0x80, 0x1F, 0x01, 0x00, 0x00, 0x40};
    set.expect(HERO_TEST, heroTest);
    // mov ecx, [TheGameText] / mov eax, [ecx] / call [eax + 0x44] / push eax / lea eax, [ebp - 0x28] / push eax /
    // call UnicodeString::format: a fetched string is the format argument
    const BYTE stockLine[21] = {
        0x8B, 0x0D, 0x04, 0x4B, 0xDE, 0x00, 0x8B, 0x01, 0xFF, 0x50, 0x44, 0x50, 0x8D, 0x45, 0xD8, 0x50, 0xE8, 0x1C, 0x74, 0x2D, 0x00
    };
    set.expect(STOCK_LINE, stockLine);
    // mov [0x00D9F608], esi: the logic frame rate
    const BYTE frameRate[6] = {0x89, 0x35, 0x08, 0xF6, 0xD9, 0x00};
    set.expect(FRAME_RATE_SETUP, frameRate);

    // push 1 / push 0 / lea eax, [ebp - 0xC] / push eax / push 0xC / mov ecx, ebx: the object / ... / call getModifierMultiplier
    const BYTE rechargeMultiplier[23] = {
        0x6A, 0x01, 0x6A, 0x00, 0x8D, 0x45, 0xF4, 0x50, 0x6A, 0x0C, 0x8B, 0xCB, 0xF3, 0x0F, 0x11, 0x45, 0xF4, 0xE8, 0x88, 0x59,
        0xDF, 0xFF, 0xF3
    };
    set.expect(RECHARGE_FORMULA + 8, rechargeMultiplier);
    // mov eax, [eax + 0x18] / shr eax, 5 / test al, 1 / je / mov ecx, [ebp - 4] / call discount / fadd [1.0f] / fstp [ebp - 8]
    const BYTE rechargeDiscount[24] = {
        0x8B, 0x40, 0x18, 0xC1, 0xE8, 0x05, 0xA8, 0x01, 0x74, 0x11, 0x8B, 0x4D, 0xFC, 0xE8, 0x06, 0x3C, 0xE1, 0xFF, 0xD8, 0x05,
        0x08, 0x19, 0xBD, 0x00
    };
    set.expect(RECHARGE_FORMULA + 0x33, rechargeDiscount);
    // mov eax, [eax + 0x20]: ReloadTime / ... / fild / jge / fadd [2^32] / fmul [ebp - 8] / fmul [ebp - 0xC] / call ftol
    const BYTE rechargeFrames[24] = {
        0x8B, 0x40, 0x20, 0x85, 0xC0, 0x89, 0x45, 0xFC, 0xDB, 0x45, 0xFC, 0x7D, 0x06, 0xD8, 0x05, 0x98, 0x86, 0xBD, 0x00, 0xD8,
        0x4D, 0xF8, 0xD8, 0x4D
    };
    set.expect(RECHARGE_FORMULA + 0x5C, rechargeFrames);
    const BYTE rechargeFtol[6] = {0xF4, 0xE8, 0xA3, 0x60, 0x1A, 0x00};
    set.expect(RECHARGE_FORMULA + 0x74, rechargeFtol);
    // cmp dword [esi + 0xC], 0: paused / jne / mov eax, [TheGameLogic] / mov eax, [eax + 0x40] / cmp eax, [esi + 8]: ready frame
    const BYTE readyTest[17] = {0x83, 0x7E, 0x0C, 0x00, 0x75, 0x12, 0xA1, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x40, 0x40, 0x3B, 0x46, 0x08};
    set.expect(READY_TEST, readyTest);
    const BYTE readyTestAlt[17] = {0x83, 0x7E, 0x08, 0x00, 0x75, 0x12, 0xA1, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x40, 0x40, 0x3B, 0x46, 0x04};
    set.expect(READY_TEST_ALT, readyTestAlt);
    // push ebp / mov ebp, esp / sub esp, 0xC / push esi / mov esi, ecx / push edi / mov edi, [esi - 0xC]
    const BYTE startRecharge[13] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7E, 0xF4};
    set.expect(START_POWER_RECHARGE, startRecharge);
    // ... / mov edi, [esi - 0x20]
    const BYTE startRechargeAlt[13] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7E, 0xE0};
    set.expect(START_POWER_RECHARGE_ALT, startRechargeAlt);

    const BYTE getThingTemplate[6] = {0x56, 0x8D, 0x71, 0x20, 0x8B, 0xCE};
    set.expect(GET_THING_TEMPLATE, getThingTemplate);
    // cmp dword [esp + 4], 0 / jne
    const BYTE getSpecialPowerModule[6] = {0x83, 0x7C, 0x24, 0x04, 0x00, 0x75};
    set.expect(GET_SPECIAL_POWER_MODULE, getSpecialPowerModule);
    // call: the modifier holder / test eax, eax / jne / xor al, al / ret 0x10
    const BYTE getModifierMultiplier[12] = {0xE8, 0x74, 0xFC, 0xFF, 0xFF, 0x85, 0xC0, 0x75, 0x05, 0x32, 0xC0, 0xC2};
    set.expect(GET_MODIFIER_MULTIPLIER, getModifierMultiplier);
    // mov ecx, [ecx + 0x31C] / test ecx, ecx
    const BYTE controllingPlayer[8] = {0x8B, 0x89, 0x1C, 0x03, 0x00, 0x00, 0x85, 0xC9};
    set.expect(CONTROLLING_PLAYER, controllingPlayer);
    // mov eax, ecx / mov edx, [eax + 4] / test edx, edx
    const BYTE finalOverride[7] = {0x8B, 0xC1, 0x8B, 0x50, 0x04, 0x85, 0xD2};
    set.expect(FINAL_OVERRIDE, finalOverride);
    // fld dword [ecx + 0x718] / ret
    const BYTE rechargeDiscountRead[7] = {0xD9, 0x81, 0x18, 0x07, 0x00, 0x00, 0xC3};
    set.expect(PLAYER_RECHARGE_DISCOUNT, rechargeDiscountRead);
    // the two hasUpgrade functions differ only in the call
    const BYTE playerHasUpgrade[23] = {
        0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x75, 0x04, 0x32, 0xC0, 0xEB, 0x08, 0xFF, 0x70, 0x38, 0xE8, 0xEE, 0xEF, 0xFF, 0xFF,
        0xC2, 0x04, 0x00
    };
    set.expect(PLAYER_HAS_UPGRADE, playerHasUpgrade);
    const BYTE objectHasUpgrade[23] = {
        0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x75, 0x04, 0x32, 0xC0, 0xEB, 0x08, 0xFF, 0x70, 0x38, 0xE8, 0x0B, 0xCC, 0xFF, 0xFF,
        0xC2, 0x04, 0x00
    };
    set.expect(OBJECT_HAS_UPGRADE, objectHasUpgrade);
    // push ebp / mov ebp, esp / cmp dword [ebp + 0x10], -1
    const BYTE thingBuildTime[9] = {0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x10, 0xFF, 0x53, 0x56};
    set.expect(THING_BUILD_TIME, thingBuildTime);
    // push esi / mov esi, ecx / mov ecx, [esp + 8]
    const BYTE upgradeBuildTime[7] = {0x56, 0x8B, 0xF1, 0x8B, 0x4C, 0x24, 0x08};
    set.expect(UPGRADE_BUILD_TIME, upgradeBuildTime);
    // ... / test byte [template + 0x117], 8: KINDOF SPELL_BOOK
    const BYTE spellBookFinder[24] = {
        0x57, 0x8B, 0x7C, 0x24, 0x08, 0x85, 0xFF, 0x75, 0x05, 0x33, 0xC0, 0x40, 0x5F, 0xC3, 0x56, 0x8B, 0x74, 0x24, 0x10, 0x83,
        0x7E, 0x04, 0x00, 0x75
    };
    set.expect(SPELL_BOOK_FINDER, spellBookFinder);
    const BYTE spellBookKind[7] = {0xF6, 0x80, 0x17, 0x01, 0x00, 0x00, 0x08};
    set.expect(SPELL_BOOK_FINDER + 0x1C, spellBookKind);
    // push esi / push edi / mov edi, ecx / mov eax, [edi + 0x34C]: the teams / mov esi, [eax]
    const BYTE forEachTeamObject[12] = {0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x87, 0x4C, 0x03, 0x00, 0x00, 0x8B, 0x30};
    set.expect(FOR_EACH_TEAM_OBJECT, forEachTeamObject);
    // mov eax, [esp + 8] / mov eax, [eax] / test eax, eax / je: the format is a UnicodeString
    const BYTE format[10] = {0x8B, 0x44, 0x24, 0x08, 0x8B, 0x00, 0x85, 0xC0, 0x74, 0x13};
    set.expect(UNICODE_STRING_FORMAT, format);
    // mov eax, [esp + 4] / mov eax, [eax] / test eax, eax / je / movzx
    const BYTE append[12] = {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x00, 0x85, 0xC0, 0x74, 0x06, 0x0F, 0xB7};
    set.expect(UNICODE_STRING_APPEND, append);
    // push esi / mov esi, [esp + 8] / test esi, esi / push edi / mov edi, ecx / je
    const BYTE appendWide[12] = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x57, 0x8B, 0xF9, 0x74, 0x0A};
    set.expect(UNICODE_STRING_APPEND_WIDE, appendWide);
    const BYTE release[8] = {0x6A, 0xFF, 0x68, 0xF8, 0x0E, 0xB7, 0x00, 0x64};
    set.expect(UNICODE_STRING_RELEASE, release);
    // push ebp / mov ebp, esp / sub esp, 0x20
    const BYTE ftol[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20};
    set.expect(FTOL, ftol);
    const BYTE newline[4] = {0x0A, 0x00, 0x00, 0x00};
    set.expect(WIDE_NEWLINE, newline);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
