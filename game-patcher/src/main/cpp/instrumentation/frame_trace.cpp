#include "features.h"

#if GAME_PATCHER_FRAME_PACING_TRACE

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "instrumentation/frame_trace.h"

// Frame pacing trace (measuring builds only, GAME_PATCHER_FRAME_PACING_TRACE)
//
// Records when each pass starts its client update (the time its camera step is for), the step's scale, whether the pass
// advances a sub-frame and the tactical view's camera as the pass before left it (the camera of the picture before: position,
// angle, pitch, zoom, current height above the ground; View's fields, 1.06 layout, the angle checked at 0x006DA7A2); when the
// render thread's worker starts and ends each Present, with the pass and the refresh the picture
// pacer gave it (when it did); and when the display's vertical blanks begin (D3DKMTWaitForVerticalBlankEvent on the primary
// display, from a thread of its own). Recording starts 10 s after the match does (logic frame 2), lasts 20 s, and is then
// written to %TEMP%\bfme2-frame-trace.csv (times in microseconds).

namespace {

    constexpr uintptr_t GAME_LOGIC = 0x00DE412C;
    constexpr uintptr_t LOGIC_FRAME_OFFSET = 0x40;
    constexpr uintptr_t TACTICAL_VIEW = 0x00DE447C;
    // x, y, z; angle, pitch; zoom; the current height above the ground
    constexpr uintptr_t CAMERA_FIELDS[7] = {0x0C, 0x10, 0x14, 0x28, 0x2C, 0x3C, 0x50};
    constexpr int CAPACITY = 16384;
    constexpr int64_t SKIP_SECONDS = 10;
    constexpr int64_t RECORD_SECONDS = 20;

    enum State : LONG { WAITING, SKIPPING, RECORDING, DONE };

    struct Pass {
        int64_t at;
        float scale;
        uint32_t advances;
        uint32_t logicFrame;
        float camera[7];
    };

    Pass passes[CAPACITY];
    int passCount = 0;
    int64_t presentStarts[CAPACITY];
    int64_t presentEnds[CAPACITY];
    int64_t presentPasses[CAPACITY];
    int64_t presentSlots[CAPACITY];
    volatile LONG presentCount = 0;
    // the worker's: the picture of the next Present
    int64_t pacedPass = 0;
    int64_t pacedSlot = 0;
    int64_t vblanks[CAPACITY];
    volatile LONG vblankCount = 0;
    volatile LONG state = WAITING;
    int64_t frequency = 0;
    int64_t recordFrom = 0;

    struct OpenAdapterFromHdc {
        HDC hdc;
        UINT adapter;
        LUID luid;
        UINT source;
    };

    struct WaitForVerticalBlank {
        UINT adapter;
        UINT device;
        UINT source;
    };

    using OpenAdapter = LONG (APIENTRY *)(OpenAdapterFromHdc *);
    using WaitForVblank = LONG (APIENTRY *)(const WaitForVerticalBlank *);

    int64_t now() {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    uint32_t logicFrame() {
        const auto *logic = *reinterpret_cast<const uint8_t *const *>(GAME_LOGIC);
        return logic != nullptr ? *reinterpret_cast<const uint32_t *>(logic + LOGIC_FRAME_OFFSET) : 0;
    }

    DWORD WINAPI watchVblanks(LPVOID) {
        const HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
        const auto open = gdi != nullptr ? reinterpret_cast<OpenAdapter>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc")) : nullptr;
        const auto wait = gdi != nullptr ? reinterpret_cast<WaitForVblank>(GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent")) : nullptr;
        if (open == nullptr || wait == nullptr) {
            return 0;
        }
        OpenAdapterFromHdc adapter = {};
        adapter.hdc = GetDC(nullptr);
        const LONG opened = open(&adapter);
        ReleaseDC(nullptr, adapter.hdc);
        if (opened != 0) {
            return 0;
        }
        const WaitForVerticalBlank request = {adapter.adapter, 0, adapter.source};
        while (state != DONE) {
            if (wait(&request) != 0) {
                return 0;
            }
            const int64_t at = now();
            if (state == RECORDING && vblankCount < CAPACITY) {
                vblanks[vblankCount] = at;
                vblankCount = vblankCount + 1;
            }
        }
        return 0;
    }

    int64_t micros(const int64_t ticks) {
        return (ticks - recordFrom) * 1000000 / frequency;
    }

    void write() {
        wchar_t path[MAX_PATH];
        const DWORD length = GetTempPathW(MAX_PATH, path);
        if (length == 0 || length + 32 > MAX_PATH) {
            return;
        }
        wcscat_s(path, L"bfme2-frame-trace.csv");
        FILE *file = nullptr;
        if (_wfopen_s(&file, path, L"w") != 0 || file == nullptr) {
            return;
        }
        fprintf(file, "kind,at,end,scale,advances,logic,pass,slot,x,y,z,angle,pitch,zoom,height\n");
        for (int i = 0; i < passCount; i++) {
            const float *camera = passes[i].camera;
            fprintf(file, "pass,%lld,,%.6f,%u,%u,,,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.4f\n", micros(passes[i].at), passes[i].scale, passes[i].advances,
                    passes[i].logicFrame, camera[0], camera[1], camera[2], camera[3], camera[4], camera[5], camera[6]);
        }
        for (LONG i = 0; i < presentCount; i++) {
            if (presentPasses[i] != 0) {
                fprintf(file, "present,%lld,%lld,,,,%lld,%lld\n", micros(presentStarts[i]), micros(presentEnds[i]), micros(presentPasses[i]),
                        presentSlots[i]);
            } else {
                fprintf(file, "present,%lld,%lld,,,,,\n", micros(presentStarts[i]), micros(presentEnds[i]));
            }
        }
        for (LONG i = 0; i < vblankCount; i++) {
            fprintf(file, "vblank,%lld,,,,,,\n", micros(vblanks[i]));
        }
        fclose(file);
    }
}

namespace instrumentation {
    namespace frame_trace {

        void pass(const int64_t at, const float scale, const bool advances) {
            if (state == WAITING) {
                if (logicFrame() < 2) {
                    return;
                }
                LARGE_INTEGER counter;
                QueryPerformanceFrequency(&counter);
                frequency = counter.QuadPart;
                recordFrom = at + SKIP_SECONDS * frequency;
                state = SKIPPING;
                CloseHandle(CreateThread(nullptr, 0, watchVblanks, nullptr, 0, nullptr));
            }
            if (state == SKIPPING) {
                if (at < recordFrom) {
                    return;
                }
                state = RECORDING;
            }
            if (state != RECORDING) {
                return;
            }
            if (at - recordFrom >= RECORD_SECONDS * frequency || passCount == CAPACITY) {
                state = DONE;
                // the worker and the vblank thread see DONE before their next entry
                Sleep(50);
                write();
                return;
            }
            Pass &recorded = passes[passCount++];
            recorded = {at, scale, advances ? 1u : 0u, logicFrame(), {}};
            const auto *view = *reinterpret_cast<const uint8_t *const *>(TACTICAL_VIEW);
            if (view != nullptr) {
                for (int i = 0; i < 7; i++) {
                    recorded.camera[i] = *reinterpret_cast<const float *>(view + CAMERA_FIELDS[i]);
                }
            }
        }

        void paced(const int64_t passStart, const int64_t slot) {
            pacedPass = passStart;
            pacedSlot = slot;
        }

        void presented(const int64_t before, const int64_t after) {
            const int64_t pass = pacedPass;
            const int64_t slot = pacedSlot;
            pacedPass = 0;
            pacedSlot = 0;
            if (state != RECORDING || presentCount >= CAPACITY) {
                return;
            }
            presentStarts[presentCount] = before;
            presentEnds[presentCount] = after;
            presentPasses[presentCount] = pass;
            presentSlots[presentCount] = slot;
            presentCount = presentCount + 1;
        }
    }
}

#endif
