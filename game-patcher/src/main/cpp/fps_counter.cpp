#include <windows.h>
#include <cstdint>

#include "display_text.h"
#include "fps_counter.h"
#include "patch_set.h"

// FPS counter
//
// Shows the frames drawn per second in the top left corner when the game is started with -showfps (patches.cpp checks for it).
//
// W3DDisplay::draw (0x00449CF8) draws the views and the windows, then the text of a debug view when one is active (0x0044A21B,
// 0x00449C5A) and ends the frame (0x0044A227). The hook in front of the debug view draws a line of text (display_text.h). The count
// is of the frames that reach the hook, measured over half a second. Only the client's screen changes.

using namespace patching;

namespace {

    constexpr uintptr_t DEBUG_VIEW_TEST = 0x0044A21B;
    constexpr uintptr_t DEBUG_VIEW_DRAW = 0x0044A220;
    constexpr uintptr_t FRAME_END = 0x0044A227;
    constexpr int POSITION = 3;
    constexpr uint32_t COLOR = 0xFFFFFF00;

    void *counterText;
    LONGLONG frequency;
    LONGLONG windowStart;
    unsigned frames;
    unsigned shownRate;

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
            counterText = display_text::create(COLOR);
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
                display_text::setNumber(counterText, L"%u FPS", rate);
            }
        }
        display_text::draw(counterText, POSITION, POSITION);
    }
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

    display_text::expect(set);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
