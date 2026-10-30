#include <windows.h>
#include <cstdint>
#include <cwchar>

#include "fps_counter.h"
#include "patch_set.h"

// FPS counter
//
// Shows the frames drawn per second in the top left corner when the game is started with -showfps (the updater adds it in debug
// mode). The game's command line parser skips arguments it does not know (0x007BA7E1), so the game itself ignores it.
//
// W3DDisplay::draw (0x00449CF8) draws the views and the windows, then the text of a debug view when one is active (0x0044A21B,
// 0x00449C5A) and ends the frame (0x0044A227). The hook in front of the debug view draws a DisplayString made the way that debug
// view makes its own (0x0044A2C7): TheDisplayStringManager's newDisplayString (vtable + 0x38), a font from TheFontLibrary
// (0x006186E0), setText (+ 0x04), setColors (+ 0x28) and draw with a drop shadow (+ 0x38), on the W3DDisplayString vtable
// (0x00BE58D8). The count is of the frames that reach the hook, measured over half a second. Only the client's screen changes.

using namespace patching;

namespace {

    constexpr uintptr_t DEBUG_VIEW_TEST = 0x0044A21B;
    constexpr uintptr_t DEBUG_VIEW_DRAW = 0x0044A220;
    constexpr uintptr_t FRAME_END = 0x0044A227;
    constexpr uintptr_t THE_DISPLAY_STRING_MANAGER = 0x00DE4518;
    constexpr uintptr_t THE_FONT_LIBRARY = 0x00DE3D7C;
    // the W3DDisplayStringManager vtable and its newDisplayString
    constexpr uintptr_t DISPLAY_STRING_MANAGER_VTABLE = 0x00BDDD48;
    constexpr uintptr_t NEW_DISPLAY_STRING = 0x0048FFAA;
    constexpr uint32_t NEW_DISPLAY_STRING_SLOT = 0x38;
    // thiscall(font library, const AsciiString &name, float size, bool bold), ret 0xC
    constexpr uintptr_t GET_FONT = 0x006186E0;
    // thiscall(string, const char *)
    constexpr uintptr_t ASCII_STRING_CONSTRUCT = 0x004374E0;
    // thiscall(string)
    constexpr uintptr_t ASCII_STRING_RELEASE = 0x00435D50;
    // cdecl(destination, const wchar_t *format, ...): replaces the destination
    constexpr uintptr_t UNICODE_STRING_FORMAT = 0x00ADF750;
    constexpr uintptr_t DISPLAY_STRING_VTABLE = 0x00BE58D8;
    constexpr uint32_t SET_TEXT = 0x04;
    constexpr uint32_t SET_FONT = 0x18;
    constexpr uint32_t SET_COLORS = 0x28;
    constexpr uint32_t DRAW = 0x38;
    // thiscall(string, UnicodeString text), ret 4: releases the text it is handed
    constexpr uintptr_t DISPLAY_STRING_SET_TEXT = 0x0075A4F4;
    constexpr uintptr_t DISPLAY_STRING_SET_FONT = 0x00505FA8;
    constexpr uintptr_t DISPLAY_STRING_SET_COLORS = 0x0050569A;
    constexpr uintptr_t DISPLAY_STRING_DRAW = 0x00505CFA;

    constexpr wchar_t ARGUMENT[] = L"-showfps";
    constexpr char FONT_NAME[] = "Arial";
    constexpr float FONT_SIZE = 12.0f;
    constexpr bool FONT_BOLD = true;
    constexpr int POSITION = 3;
    constexpr int DROP_OFFSET = 1;
    constexpr uint32_t COLOR = 0xFFFFFF00;
    constexpr uint32_t DROP_COLOR = 0xFF000000;

    typedef void *(__fastcall *NewDisplayString)(void *manager, void *);
    typedef void *(__fastcall *GetFont)(void *library, void *, const void *name, float size, bool bold);
    typedef void (__fastcall *AsciiStringConstruct)(void **string, void *, const char *text);
    typedef void (__fastcall *StringRelease)(void **string, void *);
    typedef void (__cdecl *Format)(void **destination, const wchar_t *format, unsigned value);
    // the UnicodeString is passed by value, and is a single pointer
    typedef void (__fastcall *SetText)(void *displayString, void *, void *text);
    typedef void (__fastcall *SetFont)(void *displayString, void *, void *font);
    typedef void (__fastcall *SetColors)(void *displayString, void *, uint32_t color, uint32_t dropColor);
    typedef void (__fastcall *Draw)(void *displayString, void *, int x, int y, int xDrop, int yDrop);

    void *counterText;
    LONGLONG frequency;
    LONGLONG windowStart;
    unsigned frames;
    unsigned shownRate;

    template<typename T>
    T slot(const void *object, const uint32_t offset) {
        return reinterpret_cast<T>(*reinterpret_cast<const uintptr_t *>(*reinterpret_cast<const uint8_t *const *>(object) + offset));
    }

    void *createCounterText() {
        void *manager = *reinterpret_cast<void **>(THE_DISPLAY_STRING_MANAGER);
        void *library = *reinterpret_cast<void **>(THE_FONT_LIBRARY);
        if (manager == nullptr || library == nullptr) {
            return nullptr;
        }
        void *text = slot<NewDisplayString>(manager, NEW_DISPLAY_STRING_SLOT)(manager, nullptr);
        if (text == nullptr) {
            return nullptr;
        }
        void *name = nullptr;
        reinterpret_cast<AsciiStringConstruct>(ASCII_STRING_CONSTRUCT)(&name, nullptr, FONT_NAME);
        void *font = reinterpret_cast<GetFont>(GET_FONT)(library, nullptr, &name, FONT_SIZE, FONT_BOLD);
        reinterpret_cast<StringRelease>(ASCII_STRING_RELEASE)(&name, nullptr);
        if (font != nullptr) {
            slot<SetFont>(text, SET_FONT)(text, nullptr, font);
        }
        slot<SetColors>(text, SET_COLORS)(text, nullptr, COLOR, DROP_COLOR);
        return text;
    }

    void __cdecl drawCounter() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (windowStart == 0) {
            windowStart = now.QuadPart;
        }
        frames++;
        const LONGLONG elapsed = now.QuadPart - windowStart;
        const bool measured = elapsed >= frequency / 2;

        if (counterText == nullptr) {
            counterText = createCounterText();
            if (counterText == nullptr) {
                return;
            }
        }
        if (measured) {
            const auto rate = static_cast<unsigned>((frames * frequency + elapsed / 2) / elapsed);
            frames = 0;
            windowStart = now.QuadPart;
            if (rate != shownRate) {
                shownRate = rate;
                void *text = nullptr;
                reinterpret_cast<Format>(UNICODE_STRING_FORMAT)(&text, L"%u FPS", rate);
                slot<SetText>(counterText, SET_TEXT)(counterText, nullptr, text);
            }
        }
        slot<Draw>(counterText, DRAW)(counterText, nullptr, POSITION, POSITION, DROP_OFFSET, DROP_OFFSET);
    }

    bool isArgumentEnd(const wchar_t character) {
        return character == L'\0' || character == L' ' || character == L'\t';
    }
}

bool fpsCounterRequested() {
    const wchar_t *position = GetCommandLineW();
    constexpr size_t length = sizeof(ARGUMENT) / sizeof(ARGUMENT[0]) - 1;
    bool quoted = false;
    bool argumentStart = true;
    for (; *position != L'\0'; position++) {
        if (*position == L'"') {
            quoted = !quoted;
            argumentStart = false;
            continue;
        }
        if (!quoted && (*position == L' ' || *position == L'\t')) {
            argumentStart = true;
            continue;
        }
        if (argumentStart && !quoted && _wcsnicmp(position, ARGUMENT, length) == 0 && isArgumentEnd(position[length])) {
            return true;
        }
        argumentStart = false;
    }
    return false;
}

bool applyFpsCounter() {

    LARGE_INTEGER performanceFrequency;
    if (!QueryPerformanceFrequency(&performanceFrequency) || performanceFrequency.QuadPart <= 0) {
        return false;
    }
    frequency = performanceFrequency.QuadPart;

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // In place of cmp dword [esi + 0x30], ebx / je FRAME_END: draws the counter, then makes the comparison and the jump.
    Cave cave(caveMemory);
    cave.emit8(0x60); // pushad
    cave.call(reinterpret_cast<uintptr_t>(&drawCounter));
    cave.emit8(0x61); // popad
    const BYTE debugViewTest[3] = {0x39, 0x5E, 0x30}; // cmp dword [esi + 0x30], ebx: a debug view
    cave.emit(debugViewTest);
    cave.jcc(JE, FRAME_END);
    cave.jmp(DEBUG_VIEW_DRAW);

    static PatchSet set;
    set.reset();

    // mov ecx, [0x00DE3654] / mov eax, [ecx] / call [eax + 0x30]: the windows
    const BYTE windows[11] = {0x8B, 0x0D, 0x54, 0x36, 0xDE, 0x00, 0x8B, 0x01, 0xFF, 0x50, 0x30};
    set.expect(DEBUG_VIEW_TEST - 11, windows);
    // cmp dword [esi + 0x30], ebx / je FRAME_END
    const BYTE debugViewHead[5] = {0x39, 0x5E, 0x30, 0x74, 0x07};
    set.hook(DEBUG_VIEW_TEST, debugViewHead, reinterpret_cast<uintptr_t>(caveMemory));
    // mov ecx, esi: the debug view's draw follows
    const BYTE debugViewDraw[2] = {0x8B, 0xCE};
    set.expect(DEBUG_VIEW_DRAW, debugViewDraw);
    // push edi: the frame's end follows
    const BYTE frameEnd[1] = {0x57};
    set.expect(FRAME_END, frameEnd);

    // the debug view's text, made the same way: newDisplayString through + 0x38, then setFont through + 0x18
    const BYTE debugViewText[23] = {
        0x8B, 0x0D, 0x18, 0x45, 0xDE, 0x00, 0x8B, 0x01, 0xFF, 0x50, 0x38, 0x89, 0x83, 0xCC, 0x01, 0x00, 0x00, 0x8B, 0x10, 0x57,
        0x8B, 0xC8, 0xFF
    };
    set.expect(0x0044A373, debugViewText);
    // its font: mov ecx, [TheFontLibrary] / fstp [esp] / lea eax, [ebp - 0x24] / push eax / mov [ebp - 4], esi / call getFont
    const BYTE debugViewFont[21] = {
        0x8B, 0x0D, 0x7C, 0x3D, 0xDE, 0x00, 0xD9, 0x1C, 0x24, 0x8D, 0x45, 0xDC, 0x50, 0x89, 0x75, 0xFC, 0xE8, 0x83, 0xE3, 0x1C,
        0x00
    };
    set.expect(0x0044A348, debugViewFont);
    // and its draw: setColors(white, black) through + 0x28, draw(3, 3, 1, 1) through + 0x38
    const BYTE debugViewDrawText[24] = {
        0x8B, 0x8B, 0xCC, 0x01, 0x00, 0x00, 0x8B, 0x01, 0x68, 0x00, 0x00, 0x00, 0xFF, 0x6A, 0xFF, 0xFF, 0x50, 0x28, 0x8B, 0x8B,
        0xCC, 0x01, 0x00, 0x00
    };
    set.expect(0x00449C1F, debugViewDrawText);
    const BYTE debugViewDrawCall[13] = {0x8B, 0x01, 0x6A, 0x01, 0x6A, 0x01, 0x6A, 0x03, 0x6A, 0x03, 0xFF, 0x50, 0x38};
    set.expect(0x00449C37, debugViewDrawCall);

    // the manager's newDisplayString, which builds a W3DDisplayString: push 0x1F8 / call / ... / call its constructor
    BYTE newDisplayStringSlot[4];
    putInt(newDisplayStringSlot, static_cast<int>(NEW_DISPLAY_STRING));
    set.expect(DISPLAY_STRING_MANAGER_VTABLE + NEW_DISPLAY_STRING_SLOT, newDisplayStringSlot);
    const BYTE newDisplayString[8] = {0xB8, 0xAB, 0x40, 0xB7, 0x00, 0xE8, 0x3C, 0xCF};
    set.expect(NEW_DISPLAY_STRING, newDisplayString);
    // mov dword [esi], the W3DDisplayString vtable, in that constructor
    const BYTE displayStringVtable[6] = {0xC7, 0x06, 0xD8, 0x58, 0xBE, 0x00};
    set.expect(0x00505981, displayStringVtable);

    // the four slots and the functions behind them
    BYTE setTextSlot[4];
    putInt(setTextSlot, static_cast<int>(DISPLAY_STRING_SET_TEXT));
    set.expect(DISPLAY_STRING_VTABLE + SET_TEXT, setTextSlot);
    BYTE setFontSlot[4];
    putInt(setFontSlot, static_cast<int>(DISPLAY_STRING_SET_FONT));
    set.expect(DISPLAY_STRING_VTABLE + SET_FONT, setFontSlot);
    BYTE setColorsSlot[4];
    putInt(setColorsSlot, static_cast<int>(DISPLAY_STRING_SET_COLORS));
    set.expect(DISPLAY_STRING_VTABLE + SET_COLORS, setColorsSlot);
    BYTE drawSlot[4];
    putInt(drawSlot, static_cast<int>(DISPLAY_STRING_DRAW));
    set.expect(DISPLAY_STRING_VTABLE + DRAW, drawSlot);
    // ... / lea ecx, [ebp + 8]: the text / call releaseBuffer / ... / ret 4
    const BYTE setTextRelease[16] = {0x83, 0x4D, 0xFC, 0xFF, 0x8D, 0x4D, 0x08, 0xE8, 0x7C, 0xC2, 0xCD, 0xFF, 0x8B, 0x4D, 0xF4, 0x5F};
    set.expect(DISPLAY_STRING_SET_TEXT + 0x34, setTextRelease);
    const BYTE setFont[8] = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x85, 0xFF};
    set.expect(DISPLAY_STRING_SET_FONT, setFont);
    // mov edx, [esp + 4] / push esi / push 4 / lea eax, [ecx + 0x1B8]: four text colours, then four drop colours
    const BYTE setColors[13] = {0x8B, 0x54, 0x24, 0x04, 0x56, 0x6A, 0x04, 0x8D, 0x81, 0xB8, 0x01, 0x00, 0x00};
    set.expect(DISPLAY_STRING_SET_COLORS, setColors);
    const BYTE draw[8] = {0x55, 0x8B, 0xEC, 0x51, 0x51, 0x56, 0x8B, 0xF1};
    set.expect(DISPLAY_STRING_DRAW, draw);

    // push ebp / mov ebp, esp / fld [ebp + 0xC]: the size
    const BYTE getFont[6] = {0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x0C};
    set.expect(GET_FONT, getFont);
    // ret 0xC
    const BYTE getFontReturn[3] = {0xC2, 0x0C, 0x00};
    set.expect(GET_FONT + 0x3F, getFontReturn);
    const BYTE asciiStringConstruct[8] = {0x8B, 0x54, 0x24, 0x04, 0x85, 0xD2, 0x56, 0x8B};
    set.expect(ASCII_STRING_CONSTRUCT, asciiStringConstruct);
    const BYTE asciiStringRelease[7] = {0x6A, 0xFF, 0x68, 0xF8, 0x0E, 0xB7, 0x00};
    set.expect(ASCII_STRING_RELEASE, asciiStringRelease);
    // mov ecx, [esp + 8]: the format / lea eax, [esp + 0xC]: the values / push eax / push ecx
    const BYTE format[10] = {0x8B, 0x4C, 0x24, 0x08, 0x8D, 0x44, 0x24, 0x0C, 0x50, 0x51};
    set.expect(UNICODE_STRING_FORMAT, format);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
