#include <windows.h>
#include <cstdint>

#include "frame_rate_uncap.h"
#include "patch_set.h"

// Uncapped frame rate (being built, off by default; tools/analysis/notes/21-uncap-frame-rate.md)
//
// Problem: the game draws one frame per sub-frame of its logic, 60 a second with render_rate, however fast the machine is.
// Fix: draws as many frames as the machine and the display allow, and advances a sub-frame only when one is due (every 16.5 ms,
// divided by the network slowdown as the stock limiter divides its period), so the logic keeps its pace and its steps. The
// frames in between take the engine's own path for a frame that only draws (the script debugger's pause in the stock game):
// the client half runs, the sub-frame and the client frame counter stay. So far only the scheduling: what the client update
// advances per sub-frame (the W3D clock, the interpolation, the drawables) is not carried yet, so those frames repeat the picture.
//
//   0x006325C4  the call of the "frame only draws" predicate in the engine's pass (0x006325A0): decides each pass
//   0x00441822  the call of that pass in GameEngine::update: after it, whether a sub-frame really ran (GameClient + 0xC8)
//   0x0063A06E  the pace code recomputes the network slowdown while the sub-frame is 1, i.e. on every pass that only draws after
//               a wrap too: once per logic frame instead
//   0x0063A1B2  behind render_rate's limiter period: the display's cap instead, and the W3D step per client frame
//   0x0048E2D3  the script fade's once per logic frame latch: once per client frame, as the frames that only draw see the same
//               sub-frame again
//
// The stock limiter does not pace a pass while its flag (0x00DE4320) is off: fast forward, the network catch-up window, the INI's
// UseFPSLimit off. Every pass then advances, as in the stock game. A sub-frame the logic refuses (the network not ready, a pause)
// is tried again on the next pass, as the stock game does on every frame.
//
// The approach, frames that only draw through the engine's own predicate, is the BFME2 Accelerator's (Ohad Baehr,
// https://github.com/OhadBaehr/bfme2-accelerator, aotr_logicrate.inc, MIT, THIRD-PARTY-NOTICES.txt); what such frames break was
// worked out from pySAGE's render-only-frames.md (https://github.com/ClementJ18/pySAGE) and our own census
// (tools/analysis/notes/21-uncap-phase0).

using namespace patching;

namespace {

    constexpr uintptr_t FRAME_ONLY_DRAWS_CALL = 0x006325C4;
    constexpr uintptr_t FRAME_ONLY_DRAWS = 0x00603452;
    constexpr uintptr_t ENGINE_PASS_CALL = 0x00441822;
    constexpr uintptr_t ENGINE_PASS = 0x006325A0;
    constexpr uintptr_t SLOWDOWN_RECOMPUTE = 0x0063A06E;
    constexpr uintptr_t SLOWDOWN_RECOMPUTE_RESUME = 0x0063A078;
    constexpr uintptr_t SLOWDOWN_KEPT = 0x0063A15C;
    constexpr uintptr_t LIMITER_PERIOD = 0x0063A1B2;
    constexpr uintptr_t LIMITER_PERIOD_RESUME = 0x0063A1BC;
    constexpr uintptr_t FADE_LATCH_CALL = 0x0048E2D3;
    constexpr uintptr_t LOGIC_FRAME_LATCH = 0x0063252F;

    constexpr uintptr_t GAME_CLIENT = 0x00DE4388;
    constexpr uintptr_t GAME_LOGIC = 0x00DE412C;
    constexpr uintptr_t LIMITER_ON = 0x00DE4320;
    constexpr uintptr_t SLOWDOWN_SCALAR = 0x00D9F498;
    constexpr uintptr_t W3D_STEP = 0x00DC7A8C;
    // GameClient: the client frame counter, and whether the last pass ran a sub-frame
    constexpr uintptr_t CLIENT_FRAME_OFFSET = 0x10;
    constexpr uintptr_t TIME_RAN_OFFSET = 0xC8;

    // render_rate's sub-frame: 12 of them make the stock 198 ms logic frame
    constexpr double SUB_FRAME_MS = 16.5;
    // 0: no cap besides the display's vsync
    constexpr uint32_t MAX_FRAMES_PER_SECOND = 0;
    constexpr int64_t SPIN_MARGIN_MS = 2;

    using Predicate = bool (__thiscall *)(void *);
    using Pass = void (__thiscall *)(void *);

    int64_t ticksPerSecond = 0;
    uint32_t lastLogicFrame = 0xFFFFFFFF;
    uint32_t fadeFrame = 0xFFFFFFFF;

    int64_t now() {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    uint8_t *gameClient() {
        return *reinterpret_cast<uint8_t **>(GAME_CLIENT);
    }

    uint32_t clientFrame() {
        const uint8_t *client = gameClient();
        return client != nullptr ? *reinterpret_cast<const uint32_t *>(client + CLIENT_FRAME_OFFSET) : 0;
    }

    // When a pass advances a sub-frame. Times in performance counter ticks.
    struct Pacer {
        enum Decision { ADVANCE, WAIT_THEN_ADVANCE, ONLY_DRAW };

        // when the next sub-frame is due
        int64_t next = 0;
        // what a pass that only draws takes, from its decision to the next one: rises at once, falls slowly
        int64_t cost = 0;
        int64_t passStart = 0;
        bool anchored = false;
        bool lastOnlyDrew = false;
        bool retry = false;

        Decision decide(const int64_t at, const bool limited, const int64_t interval) {
            if (lastOnlyDrew) {
                // a stall (a load, a dragged window) is not what the next frame will take
                const int64_t took = at - passStart < interval ? at - passStart : interval;
                cost = took > cost ? took : cost + (took - cost) / 16;
            }
            passStart = at;
            Decision decision;
            if (!limited) {
                anchored = false;
                decision = ADVANCE;
            } else if (!anchored || retry || at >= next) {
                decision = ADVANCE;
            } else if (at + cost >= next) {
                // logic first: a frame that only draws would end after the sub-frame is due. The estimate falls a little each
                // time, so frames that have become faster are tried again.
                decision = WAIT_THEN_ADVANCE;
                cost -= cost / 64;
            } else {
                decision = ONLY_DRAW;
            }
            lastOnlyDrew = decision == ONLY_DRAW;
            return decision;
        }

        // after a pass that tried a sub-frame, started at `at`: `ran` when it ran (a refused one is tried again on the next pass).
        // A pass a little late keeps the pace; one late by more than a sub-frame moves it, so no burst follows a stall.
        void advanced(const int64_t at, const bool ran, const int64_t interval) {
            retry = !ran;
            if (!ran) {
                return;
            }
            if (!anchored) {
                anchored = true;
                next = at + interval;
                return;
            }
            next += interval;
            if (next < at) {
                next = at;
            }
        }
    };

    Pacer pacer;
    bool passAdvances = false;
    int64_t passAt = 0;

    // the network slowdown divides the stock limiter's rate, so it stretches the sub-frame
    int64_t subFrameTicks() {
        const float slowdown = *reinterpret_cast<const float *>(SLOWDOWN_SCALAR);
        const double scale = slowdown > 0.0f ? 1.0 / slowdown : 1.0;
        return static_cast<int64_t>(SUB_FRAME_MS * scale * static_cast<double>(ticksPerSecond) / 1000.0);
    }

    void waitUntil(const int64_t target) {
        for (;;) {
            const int64_t left = target - now();
            if (left <= 0) {
                return;
            }
            const int64_t leftMs = left * 1000 / ticksPerSecond;
            Sleep(leftMs > SPIN_MARGIN_MS ? static_cast<DWORD>(leftMs - SPIN_MARGIN_MS) : 0);
        }
    }

    bool __fastcall frameOnlyDraws(void *scriptEngine) {
        if (reinterpret_cast<Predicate>(FRAME_ONLY_DRAWS)(scriptEngine)) {
            passAdvances = false;
            return true;
        }
        passAt = now();
        const Pacer::Decision decision = pacer.decide(passAt, *reinterpret_cast<const uint8_t *>(LIMITER_ON) != 0, subFrameTicks());
        if (decision == Pacer::WAIT_THEN_ADVANCE) {
            waitUntil(pacer.next);
            passAt = now();
        }
        passAdvances = decision != Pacer::ONLY_DRAW;
        return !passAdvances;
    }

    void __fastcall enginePass(void *engine) {
        reinterpret_cast<Pass>(ENGINE_PASS)(engine);
        if (!passAdvances) {
            return;
        }
        const uint8_t *client = gameClient();
        const bool ran = client != nullptr && client[TIME_RAN_OFFSET] != 0;
        pacer.advanced(passAt, ran, subFrameTicks());
    }

    // render_rate's period (eax) in, the limiter's out
    uint32_t __stdcall limiterPeriod(uint32_t) {
        // the W3D clock adds the step once per client frame: 16 and 17 ms make the 16.5 ms sub-frame
        *reinterpret_cast<int32_t *>(W3D_STEP) = 16 + static_cast<int32_t>(clientFrame() & 1);
        return MAX_FRAMES_PER_SECOND != 0 ? 1000 / MAX_FRAMES_PER_SECOND : 0;
    }

    bool __fastcall fadeLatch(void *engine) {
        if (!reinterpret_cast<Predicate>(LOGIC_FRAME_LATCH)(engine)) {
            return false;
        }
        const uint32_t frame = clientFrame();
        if (frame == fadeFrame) {
            return false;
        }
        fadeFrame = frame;
        return true;
    }

    void addSlowdownGate(PatchSet &set, Cave &cave) {
        const uintptr_t entry = cave.here();
        const BYTE subFrameOne[4] = {0x83, 0x7E, 0x34, 0x01}; // cmp dword [esi + 0x34], 1
        cave.emit(subFrameOne);
        cave.jcc(JNE, SLOWDOWN_KEPT);
        cave.emit8(0xA1); // mov eax, [TheGameLogic]
        cave.emit32(static_cast<uint32_t>(GAME_LOGIC));
        const BYTE logicFrame[3] = {0x8B, 0x40, 0x40}; // mov eax, [eax + 0x40]
        cave.emit(logicFrame);
        cave.emit8(0x3B); // cmp eax, [lastLogicFrame]
        cave.emit8(0x05);
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&lastLogicFrame)));
        cave.jcc(JE, SLOWDOWN_KEPT);
        cave.emit8(0xA3); // mov [lastLogicFrame], eax
        cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&lastLogicFrame)));
        cave.jmp(SLOWDOWN_RECOMPUTE_RESUME);

        // cmp dword [esi + 0x34], 1 / jne 0x0063A15C (xmm0 holds the slowdown there, loaded at 0x0063A043)
        const BYTE stock[10] = {0x83, 0x7E, 0x34, 0x01, 0x0F, 0x85, 0xE4, 0x00, 0x00, 0x00};
        set.hook(SLOWDOWN_RECOMPUTE, stock, entry);
        // movss xmm0, [1.0f] / movss [slowdown], xmm0: the recompute
        const BYTE recompute[8] = {0xF3, 0x0F, 0x10, 0x05, 0x08, 0x19, 0xBD, 0x00};
        set.expect(SLOWDOWN_RECOMPUTE_RESUME, recompute);
        // ucomiss xmm0, [1.0f]: where the scalar is kept
        const BYTE kept[7] = {0x0F, 0x2E, 0x05, 0x08, 0x19, 0xBD, 0x00};
        set.expect(SLOWDOWN_KEPT, kept);
    }

    void addLimiterPeriod(PatchSet &set, Cave &cave) {
        const uintptr_t entry = cave.here();
        cave.emit8(0x50); // push eax
        cave.call(reinterpret_cast<uintptr_t>(&limiterPeriod));
        const BYTE stock[10] = {
            0x8B, 0xF0,                        // mov esi, eax
            0x8B, 0xCF,                        // mov ecx, edi
            0x2B, 0x0D, 0x18, 0x43, 0xDE, 0x00 // sub ecx, [0x00DE4318]
        };
        cave.emit(stock);
        cave.jmp(LIMITER_PERIOD_RESUME);
        set.hook(LIMITER_PERIOD, stock, entry);
        // mov edx, esi: the limiter goes on with the period in esi
        const BYTE landing[2] = {0x8B, 0xD6};
        set.expect(LIMITER_PERIOD_RESUME, landing);
    }
}

bool applyFrameRateUncap() {
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        return false;
    }
    ticksPerSecond = frequency.QuadPart;

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();

    // render_rate's wrap: the sub-frames above are its 16.5 ms ones
    const BYTE wrap[3] = {0x83, 0xF8, 0x0C};
    set.expect(0x0063264A, wrap);

    const BYTE frameOnlyDrawsCall[5] = {0xE8, 0x89, 0x0E, 0xFD, 0xFF};
    set.redirectCall(FRAME_ONLY_DRAWS_CALL, frameOnlyDrawsCall, reinterpret_cast<uintptr_t>(&frameOnlyDraws));
    // mov bl, al / mov eax, [esi] / mov ecx, esi: the answer is taken from al, ecx is set again
    const BYTE answer[6] = {0x8A, 0xD8, 0x8B, 0x06, 0x8B, 0xCE};
    set.expect(FRAME_ONLY_DRAWS_CALL + 5, answer);

    const BYTE enginePassCall[5] = {0xE8, 0x79, 0x0D, 0x1F, 0x00};
    set.redirectCall(ENGINE_PASS_CALL, enginePassCall, reinterpret_cast<uintptr_t>(&enginePass));
    // push esi / mov esi, ecx in front: GameEngine::update keeps the engine in esi, which the fastcall keeps too
    const BYTE engineInEsi[3] = {0x56, 0x8B, 0xF1};
    set.expect(ENGINE_PASS_CALL - 3, engineInEsi);

    const BYTE fadeLatchCall[5] = {0xE8, 0x57, 0x42, 0x1A, 0x00};
    set.redirectCall(FADE_LATCH_CALL, fadeLatchCall, reinterpret_cast<uintptr_t>(&fadeLatch));
    // test al, al / je: the fade steps on a true answer
    const BYTE fadeAnswer[4] = {0x84, 0xC0, 0x74, 0x5B};
    set.expect(FADE_LATCH_CALL + 5, fadeAnswer);

    addSlowdownGate(set, cave);
    addLimiterPeriod(set, cave);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
