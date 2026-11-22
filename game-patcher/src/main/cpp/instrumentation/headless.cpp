#include <windows.h>
#include <cstdint>

#include "instrumentation/headless.h"
#include "patch_set.h"

// Headless (debug mode, with -headless on the game's command line; launch-test.ps1 -Headless)
//
// Lets a validation run play in the background: the game updates as usual but draws nothing, and its window starts minimised without
// taking the focus and keeps running so. Derived from pySAGE's headless patch (https://github.com/ClementJ18/pySAGE, sage_patch/docs/headless.md, section 3b),
// without its command line table: game-patcher reads -headless itself. The menus only advance when drawn, so a headless run starts
// its match without them (-file, auto_skirmish.cpp). The window, sound and resolution are the stock options' (-win, -xres, -yres,
// -noaudio, -noshellmap), which launch-test.ps1 passes with it.
//
// GameClient::update calls Display::draw at 0x00648861 (mov ecx, [TheDisplay] / mov eax, [ecx] / call [eax + 0x30]); its first
// instruction becomes a jump over the call, so the call timers' hook on the call (0x00648867) stays in place, unused. The draw on the
// loading path (0x00645CD7) is left: it is followed by a Sleep(100) anyway. Win32GameEngine::update (0x0044181F) spins on Sleep(5)
// while IsIconic says the window is minimised (0x0044183C); the branch into that loop becomes a jump past it. WinMain's window setup
// (0x0040231B, as Generals' initializeAppWindows) creates the window with WS_VISIBLE in its style (0x0040239E), calls SetFocus and
// SetForegroundWindow on it (0x00402464, 0x0040246B) and then ShowWindow with WinMain's nCmdShow (0x00402472): the style loses
// WS_VISIBLE, the two calls are left out and ShowWindow gets SW_SHOWMINNOACTIVE, so the window only appears in the taskbar. Seven
// SetWindowPos calls on the window (the setup, the display's mode changes, GameEngine::init, the main menu) activate it, as none
// passes SWP_NOACTIVATE: their flags get it (pySAGE's section 3c names 0x00402B21 for this, the ShowWindow of the second instance path).

using namespace patching;

namespace {

    constexpr uintptr_t DISPLAY_DRAW = 0x00648861;
    constexpr uintptr_t AFTER_DISPLAY_DRAW = 0x0064886C;
    constexpr uintptr_t MINIMISED_TEST = 0x0044183C;
    constexpr uintptr_t MINIMISED_BRANCH = 0x00441840;
    constexpr uintptr_t WINDOW_STYLE = 0x0040239E;
    constexpr uintptr_t WINDOW_FOCUS = 0x00402464;
    constexpr uintptr_t WINDOW_SHOW = 0x00402475;

    struct FlagsPush {
        uintptr_t address;
        BYTE flags;
    };

    // push SWP_NOSIZE | SWP_NOMOVE, or push SWP_NOMOVE | SWP_NOZORDER on the display's windowed branches
    constexpr FlagsPush SET_WINDOW_POS_FLAGS[] = {
        {0x00402450, 3}, {0x005225AA, 3}, {0x005225B6, 6}, {0x00524AD8, 3}, {0x00524AE2, 6}, {0x00636120, 3}, {0x0063618E, 3},
        {0x0063CD77, 3}, {0x0091B17A, 6}
    };
    constexpr uintptr_t SET_WINDOW_POS_CALLS[] = {0x0040245E, 0x005225C1, 0x00524AEA, 0x0063612E, 0x0063619C, 0x0063CD85, 0x0091B18D};
}

bool instrumentation::applyHeadless() {
    static PatchSet set;
    set.reset();

    // mov ecx, [TheDisplay] -> jmp 0x0064886C / nop x4
    const BYTE displayDraw[6] = {0x8B, 0x0D, 0x18, 0x44, 0xDE, 0x00};
    const BYTE skipDraw[6] = {0xEB, static_cast<BYTE>(AFTER_DISPLAY_DRAW - (DISPLAY_DRAW + 2)), 0x90, 0x90, 0x90, 0x90};
    set.replace(DISPLAY_DRAW, displayDraw, skipDraw);
    // mov ecx, [TheDisplayStringManager]: the client update goes on there
    const BYTE afterDraw[6] = {0x8B, 0x0D, 0x18, 0x45, 0xDE, 0x00};
    set.expect(AFTER_DISPLAY_DRAW, afterDraw);

    // call edi (IsIconic) / test eax, eax
    const BYTE minimisedTest[4] = {0xFF, 0xD7, 0x85, 0xC0};
    set.expect(MINIMISED_TEST, minimisedTest);
    // je 0x004418BC (not minimised: past the loop) -> jmp
    const BYTE minimisedBranch[2] = {0x74, 0x7A};
    const BYTE neverMinimised[2] = {0xEB, 0x7A};
    set.replace(MINIMISED_BRANCH, minimisedBranch, neverMinimised);

    // add eax, WS_POPUP | WS_VISIBLE | WS_SYSMENU -> WS_POPUP | WS_SYSMENU
    const BYTE windowStyle[5] = {0x05, 0x00, 0x00, 0x08, 0x90};
    const BYTE hiddenWindowStyle[5] = {0x05, 0x00, 0x00, 0x08, 0x80};
    set.replace(WINDOW_STYLE, windowStyle, hiddenWindowStyle);

    // push esi / call [SetFocus] / push esi / call [SetForegroundWindow] / push [ebp + 8] (nCmdShow)
    //   -> nop x14 / push SW_SHOWMINNOACTIVE / nop
    const BYTE windowFocus[17] = {
        0x56, 0xFF, 0x15, 0xE8, 0x08, 0xBD, 0x00, 0x56, 0xFF, 0x15, 0xE4, 0x08, 0xBD, 0x00, 0xFF, 0x75, 0x08
    };
    const BYTE minimisedWindow[17] = {
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x6A, SW_SHOWMINNOACTIVE, 0x90
    };
    set.replace(WINDOW_FOCUS, windowFocus, minimisedWindow);
    // push esi / call [ShowWindow]
    const BYTE windowShow[7] = {0x56, 0xFF, 0x15, 0xE0, 0x08, 0xBD, 0x00};
    set.expect(WINDOW_SHOW, windowShow);

    for (const FlagsPush &push : SET_WINDOW_POS_FLAGS) {
        const BYTE flags[2] = {0x6A, push.flags};
        const BYTE noActivate[2] = {0x6A, static_cast<BYTE>(push.flags | SWP_NOACTIVATE)};
        set.replace(push.address, flags, noActivate);
    }
    // call [SetWindowPos]
    const BYTE setWindowPos[6] = {0xFF, 0x15, 0xEC, 0x08, 0xBD, 0x00};
    for (const uintptr_t call : SET_WINDOW_POS_CALLS) {
        set.expect(call, setWindowPos);
    }

    return commit(set, Cave(nullptr));
}
