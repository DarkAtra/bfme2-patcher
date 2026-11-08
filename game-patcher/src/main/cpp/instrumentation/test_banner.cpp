#include <windows.h>
#include <cstdint>
#include <cwchar>

#include "display_text.h"
#include "instrumentation/test_banner.h"
#include "patch_set.h"

// Test banner (debug mode)
//
// Shows what a measuring run is doing in the top left corner, below the FPS counter, for whoever watches the game on the one
// screen: the harness writes a line (the run's label and its phase: warmup, measuring 2 of 4, done) to the file mapping
// Local\GamePatcherBanner, and the banner draws it. Without the mapping (a game the harness did not start) nothing is drawn.
//
// The line is drawn after the windows (the in-game interface), from the window manager's draw (vtable 0x00BF1DA8, stored by its
// constructor at 0x005DC338; slot + 0x30, 0x005DB725), which W3DDisplay::draw calls through [TheWindowManager] at 0x0044A218. The
// text is display_text.h's. Only the client's screen changes.

using namespace patching;

namespace {

    constexpr uintptr_t WINDOW_MANAGER_VTABLE = 0x00BF1DA8;
    constexpr uint32_t DRAW_SLOT = 0x30;
    constexpr uintptr_t WINDOW_MANAGER_DRAW = 0x005DB725;
    constexpr uintptr_t WINDOW_MANAGER_VTABLE_STORE = 0x005DC338;
    constexpr uintptr_t WINDOW_MANAGER_STORE = 0x006C15C4;
    constexpr uintptr_t DISPLAY_DRAW_WINDOWS = 0x0044A210;

    constexpr wchar_t MAPPING_NAME[] = L"Local\\GamePatcherBanner";
    constexpr size_t TEXT_LENGTH = 256;
    // draws until the mapping exists look for it this often
    constexpr unsigned LOOKUP_DRAWS = 60;
    constexpr int X = 3;
    constexpr int Y = 20;
    constexpr uint32_t COLOR = 0xFF00FFFF;

    typedef void (__fastcall *DrawWindows)(void *manager, void *);

    const wchar_t *shared;
    unsigned drawsWithoutMapping;
    wchar_t shown[TEXT_LENGTH];
    void *bannerText;

    void drawBanner() {
        if (shared == nullptr) {
            if (drawsWithoutMapping++ % LOOKUP_DRAWS != 0) {
                return;
            }
            HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MAPPING_NAME);
            if (mapping == nullptr) {
                return;
            }
            shared = static_cast<const wchar_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, TEXT_LENGTH * sizeof(wchar_t)));
            if (shared == nullptr) {
                CloseHandle(mapping);
                return;
            }
        }
        if (bannerText == nullptr) {
            bannerText = display_text::create(COLOR);
            if (bannerText == nullptr) {
                return;
            }
        }
        wchar_t current[TEXT_LENGTH];
        wcsncpy_s(current, shared, TEXT_LENGTH - 1);
        current[TEXT_LENGTH - 1] = L'\0';
        if (wcscmp(current, shown) != 0) {
            wcscpy_s(shown, current);
            display_text::setText(bannerText, shown);
        }
        if (shown[0] != L'\0') {
            display_text::draw(bannerText, X, Y);
        }
    }

    void __fastcall onDrawWindows(void *manager, void *) {
        reinterpret_cast<DrawWindows>(WINDOW_MANAGER_DRAW)(manager, nullptr);
        drawBanner();
    }
}

bool instrumentation::applyTestBanner() {

    static PatchSet set;
    set.reset();

    BYTE stockDraw[4];
    putInt(stockDraw, static_cast<int>(WINDOW_MANAGER_DRAW));
    BYTE bannerDraw[4];
    putInt(bannerDraw, static_cast<int>(reinterpret_cast<uintptr_t>(&onDrawWindows)));
    set.replace(WINDOW_MANAGER_VTABLE + DRAW_SLOT, stockDraw, bannerDraw);

    // push esi / mov esi, ecx / mov ecx, [esi + 0x30]: draws its two window lists, takes no argument
    const BYTE draw[6] = {0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x30};
    set.expect(WINDOW_MANAGER_DRAW, draw);
    // mov dword [esi], the vtable: the window manager's constructor
    const BYTE vtableStore[6] = {0xC7, 0x06, 0xA8, 0x1D, 0xBF, 0x00};
    set.expect(WINDOW_MANAGER_VTABLE_STORE, vtableStore);
    // mov [TheWindowManager], eax: the object it constructs is the one the display draws
    const BYTE windowManagerStore[5] = {0xA3, 0x54, 0x36, 0xDE, 0x00};
    set.expect(WINDOW_MANAGER_STORE, windowManagerStore);
    // mov ecx, [TheWindowManager] / mov eax, [ecx] / call [eax + 0x30]: W3DDisplay::draw draws the windows
    const BYTE displayDrawWindows[11] = {0x8B, 0x0D, 0x54, 0x36, 0xDE, 0x00, 0x8B, 0x01, 0xFF, 0x50, 0x30};
    set.expect(DISPLAY_DRAW_WINDOWS, displayDrawWindows);

    display_text::expect(set);

    return commit(set, Cave(nullptr));
}
