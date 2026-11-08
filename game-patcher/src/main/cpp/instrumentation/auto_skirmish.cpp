#include <windows.h>
#include <cstdint>

#include "game_memory.h"
#include "instrumentation/auto_skirmish.h"
#include "patch_set.h"

// Auto skirmish (debug mode, with -autoskirmish on the game's command line)
//
// Starts a skirmish without anyone at the game, for measuring runs: once the main menu stands idle it presses "Skirmish", and once
// the skirmish screen has loaded the lobby it presses "Start". The lobby is the one the screen loads from Skirmish.ini (the last
// skirmish's map, players and settings), so a run plays whatever was set up there last. Nothing is pressed twice in a process: after
// the match the menus are left alone.
//
// The screens are Apt movies whose buttons call commands registered by name; the two used here only set the screen's state:
// AptMainMenu::Skirmish (0x0091B096) sets state + 0x288 to 9 (open a screen) and + 0x28C to 7 (the skirmish screen);
// AptSkirmish::StartGame (0x009280F0) sets state + 0x6B8 to 10, which the screen's update validates and starts like the button.
// They are called from the screens' updates (vtable slot + 0x14, run every client frame while the screen is open), in front of
// the stock update: the main menu after AptMainMenu::OnInitialized set + 0x27C and its state has been 0 (idle) for IDLE_FRAMES; the
// skirmish screen once its state has been 7 (idle, the lobby loaded and shown: state 6, set once the lobby is loaded at 0x00928DE1,
// becomes 7 at 0x00928D72 when the movie is ready) for IDLE_FRAMES. A profile the screen asks for (a fresh installation) leaves it
// short of 7, and nothing is pressed.

using namespace patching;

namespace {

    constexpr uintptr_t MAIN_MENU_VTABLE = 0x00C7CDE0;
    constexpr uintptr_t MAIN_MENU_UPDATE = 0x0091C2FC;
    constexpr uintptr_t MAIN_MENU_ON_INITIALIZED = 0x0091B343;
    constexpr uintptr_t MAIN_MENU_SKIRMISH = 0x0091B096;
    constexpr uintptr_t SKIRMISH_VTABLE = 0x00C7E864;
    constexpr uintptr_t SKIRMISH_UPDATE = 0x00928CDB;
    constexpr uintptr_t SKIRMISH_LOADED = 0x00928DE1;
    constexpr uintptr_t SKIRMISH_SHOWN_STORE = 0x00928D72;
    constexpr uintptr_t SKIRMISH_START_GAME = 0x009280F0;
    constexpr uint32_t UPDATE_SLOT = 0x14;

    constexpr uint32_t MAIN_MENU_INITIALIZED = 0x27C;
    constexpr uint32_t MAIN_MENU_STATE = 0x288;
    constexpr uint32_t SKIRMISH_STATE = 0x6B8;
    constexpr uint32_t MAIN_MENU_IDLE = 0;
    constexpr uint32_t SKIRMISH_SHOWN = 7;

    // client frames a screen has to stand in its state before the button is pressed: a second at 60 fps
    constexpr uint32_t IDLE_FRAMES = 60;

    typedef uint32_t (__fastcall *Update)(void *screen, void *);
    typedef void (__fastcall *Command)(void *screen, void *, const char *argument);

    bool skirmishPressed;
    bool startPressed;
    uint32_t mainMenuIdleFrames;
    uint32_t skirmishShownFrames;

    uint32_t __fastcall onMainMenuUpdate(void *menu, void *) {
        if (!skirmishPressed && field<uint8_t>(menu, MAIN_MENU_INITIALIZED) != 0 && field<uint32_t>(menu, MAIN_MENU_STATE) == MAIN_MENU_IDLE) {
            if (++mainMenuIdleFrames >= IDLE_FRAMES) {
                skirmishPressed = true;
                reinterpret_cast<Command>(MAIN_MENU_SKIRMISH)(menu, nullptr, nullptr);
            }
        } else {
            mainMenuIdleFrames = 0;
        }
        return reinterpret_cast<Update>(MAIN_MENU_UPDATE)(menu, nullptr);
    }

    uint32_t __fastcall onSkirmishUpdate(void *screen, void *) {
        if (!startPressed && field<uint32_t>(screen, SKIRMISH_STATE) == SKIRMISH_SHOWN) {
            if (++skirmishShownFrames >= IDLE_FRAMES) {
                startPressed = true;
                reinterpret_cast<Command>(SKIRMISH_START_GAME)(screen, nullptr, nullptr);
            }
        } else {
            skirmishShownFrames = 0;
        }
        return reinterpret_cast<Update>(SKIRMISH_UPDATE)(screen, nullptr);
    }

    void replaceUpdate(PatchSet &set, const uintptr_t vtable, const uintptr_t stock, const void *replacement) {
        BYTE expected[4];
        putInt(expected, static_cast<int>(stock));
        BYTE replaced[4];
        putInt(replaced, static_cast<int>(reinterpret_cast<uintptr_t>(replacement)));
        set.replace(vtable + UPDATE_SLOT, expected, replaced);
    }
}

bool instrumentation::applyAutoSkirmish() {

    static PatchSet set;
    set.reset();

    replaceUpdate(set, MAIN_MENU_VTABLE, MAIN_MENU_UPDATE, reinterpret_cast<const void *>(&onMainMenuUpdate));
    replaceUpdate(set, SKIRMISH_VTABLE, SKIRMISH_UPDATE, reinterpret_cast<const void *>(&onSkirmishUpdate));

    // mov eax, the exception frame's handler / call / sub esp, 0x48 / push ebx / mov ebx, ecx / mov eax, [ebx + 0x288]: the state
    const BYTE mainMenuUpdate[22] = {
        0xB8, 0xCD, 0x9B, 0xBA, 0x00, 0xE8, 0xEA, 0x0B, 0x12, 0x00, 0x83, 0xEC, 0x48, 0x53, 0x8B, 0xD9, 0x8B, 0x83, 0x88, 0x02, 0x00, 0x00
    };
    set.expect(MAIN_MENU_UPDATE, mainMenuUpdate);
    // push esi / push 0 / mov esi, ecx / call / lea eax, [esi + 0x27D] / cmp byte [eax], 0 / mov byte [esi + 0x27C], 1
    const BYTE mainMenuOnInitialized[26] = {
        0x56, 0x6A, 0x00, 0x8B, 0xF1, 0xE8, 0x95, 0xFD, 0xFF, 0xFF, 0x8D, 0x86, 0x7D, 0x02, 0x00, 0x00, 0x80, 0x38, 0x00, 0xC6, 0x86, 0x7C,
        0x02, 0x00, 0x00, 0x01
    };
    set.expectBytes(MAIN_MENU_ON_INITIALIZED, mainMenuOnInitialized);
    // mov dword [ecx + 0x288], 9 / mov dword [ecx + 0x28C], 7 / ret 4
    const BYTE mainMenuSkirmish[23] = {
        0xC7, 0x81, 0x88, 0x02, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xC7, 0x81, 0x8C, 0x02, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xC2, 0x04, 0x00
    };
    set.expect(MAIN_MENU_SKIRMISH, mainMenuSkirmish);

    // push ebx / push esi / mov esi, ecx / push edi / lea edi, [esi + 0x6C1]
    const BYTE skirmishUpdate[11] = {0x53, 0x56, 0x8B, 0xF1, 0x57, 0x8D, 0xBE, 0xC1, 0x06, 0x00, 0x00};
    set.expect(SKIRMISH_UPDATE, skirmishUpdate);
    // mov eax, [esi + 0x6B8]: the state the update switches on
    const BYTE skirmishState[6] = {0x8B, 0x86, 0xB8, 0x06, 0x00, 0x00};
    set.expect(SKIRMISH_UPDATE + 0x1E, skirmishState);
    // mov dword [esi + 0x6B8], 6: the lobby loaded
    const BYTE skirmishLoaded[10] = {0xC7, 0x86, 0xB8, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00};
    set.expect(SKIRMISH_LOADED, skirmishLoaded);
    // mov dword [esi + 0x6B8], 7: shown, the state the screen waits for input in
    const BYTE skirmishShown[10] = {0xC7, 0x86, 0xB8, 0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00};
    set.expect(SKIRMISH_SHOWN_STORE, skirmishShown);
    // mov dword [ecx + 0x6B8], 10 / ret 4
    const BYTE skirmishStartGame[13] = {0xC7, 0x81, 0xB8, 0x06, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xC2, 0x04, 0x00};
    set.expect(SKIRMISH_START_GAME, skirmishStartGame);

    return commit(set, Cave(nullptr));
}
