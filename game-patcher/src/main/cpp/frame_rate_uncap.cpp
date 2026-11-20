#include <windows.h>
#include <cstdint>

#include "frame_rate_uncap.h"
#include "features.h"
#include "instrumentation/frame_trace.h"
#include "patch_set.h"
#include "picture_pacer.h"
#include "render_rate.h"

// Uncapped frame rate (tools/analysis/notes/21-uncap-frame-rate.md)
//
// Problem: the game draws one frame per sub-frame of its logic, 60 a second with render_rate, however fast the machine is.
// Fix: draws as many frames as the machine and the display allow, and advances a sub-frame only when one is due (every 16.5 ms,
// divided by the network slowdown as the stock limiter divides its period), so the logic keeps its pace and its steps. The
// frames in between take the engine's own path for a frame that only draws (the script debugger's pause in the stock game):
// the client half runs, the sub-frame and the client frame counter stay. Such a frame shows the game as far on as the time since
// the last sub-frame says (`frac` of the way to the next): the interpolation alpha the drawables are placed by, and the W3D clock
// that animates models, particles and effects, are moved on by that share of their step, one step behind the stock values so that
// they only ever rise and meet the stock value on the frame that advances. The drawables' interpolated transforms are cached per
// drawn frame instead of per client frame. The logic sees only stock values: the alpha is computed again right before each
// dispatch (0x0063256F), and a frame that advances draws with the stock alpha and clock.
// The camera steps render_rate scales (scroll, rotate keys, the War of the Ring camera and its drag totals) run on every drawn
// frame, so their scale follows the time instead of render_rate's fixed 0.5: the camera moves at the stock game's speed per
// second at any frame rate. With the render thread, the pictures are held to the display's refreshes (picture_pacer.cpp) and the
// camera steps by the time between their refreshes, so it is where it should be when each picture is seen; otherwise by the time
// between the passes.
//
//   0x006325C4  the call of the "frame only draws" predicate in the engine's pass (0x006325A0): decides each pass
//   0x00441822  the call of that pass in GameEngine::update: after it, whether a sub-frame really ran (GameClient + 0xC8)
//   0x0063A06E  the pace code recomputes the network slowdown while the sub-frame is 1, i.e. on every pass that only draws after
//               a wrap too: once per logic frame instead
//   0x0063A1B2  behind render_rate's limiter period: the display's cap instead, and the W3D step per client frame
//   0x0048E2D3  the script fade's once per logic frame latch: once per client frame, as the frames that only draw see the same
//               sub-frame again
//   0x0044B925  the W3D clock's setter call in W3DDisplay::draw: the clock a frame shows
//   0x006765D5  the drawable transform cache's stamp (Drawable::getTransformMatrix): a counter of drawn frames instead of the
//               client frame
//   0x0067BF39  the call of Drawable::calcPhysicsXform (0x0067BDC0) in applyPhysicsXform: the tilt a locomotor gives a body (ships
//               rock, vehicles pitch) is computed once per client frame and refused on a second call in it, so a frame that only
//               draws showed the body level; the last tilt computed in this client frame is applied again
//               (pySAGE's render-only-frames.md, section 2, keeps it in an enlarged DrawableLocoInfo; here a side table)
//   0x005F51D2  the particle system manager's loop, as it takes up a system to update (30 times a second, render_rate's gate):
//               each particle's position (+0x1C, the list behind the storage at system + 0xA4: its vtable + 0x20, then + 0x64)
//               is kept first (the update call itself, 0x005F51E6, is the call timers')
//   0x00961F74  the default particle draw (DefaultDraw, vtable 0x00C326B8, slot 4) copying a particle's position into the point
//               array: the position kept before the step, `share` of the way to the current one, so CPU particles move on every
//               drawn frame instead of 30 times a second (one step behind, exact at the steps). A particle without a position kept in
//               this step (just born, its slot taken) or one that moved further than its speed allows (its address reused by a new
//               one) is drawn where it is, as without this.
//
// The stock limiter does not pace a pass while its flag (0x00DE4320) is off: fast forward, the network catch-up window, the INI's
// UseFPSLimit off. Every pass then advances, as in the stock game. A sub-frame the logic refuses (the network not ready, a pause)
// is tried again on the next pass, as the stock game does on every frame.
//
// The approach, frames that only draw through the engine's own predicate, is the BFME2 Accelerator's (Ohad Baehr,
// https://github.com/OhadBaehr/bfme2-accelerator, aotr_logicrate.inc, MIT, THIRD-PARTY-NOTICES.txt); what such frames break was
// worked out from pySAGE's render-only-frames.md (https://github.com/ClementJ18/pySAGE) and our own census
// (tools/analysis/notes/21-uncap-phase0). The camera's usual frame time is the accelerator's too (aotr_scroll.inc).

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
    constexpr uintptr_t SYNC_TIME_SET_CALL = 0x0044B925;
    constexpr uintptr_t SYNC_TIME_SET = 0x00516E20;
    constexpr uintptr_t SYNC_TIME = 0x00DD1E0C;
    constexpr uintptr_t TRANSFORM_STAMP = 0x006765D5;
    constexpr uintptr_t PHYSICS_XFORM_CALL = 0x0067BF39;
    constexpr uintptr_t PHYSICS_XFORM = 0x0067BDC0;
    constexpr uintptr_t PARTICLE_SYSTEM_TAKEN = 0x005F51D2;
    constexpr uintptr_t PARTICLE_SYSTEM_CHECKED = 0x005F51D7;
    constexpr uintptr_t PARTICLE_DRAW = 0x00961F74;
    constexpr uintptr_t PARTICLE_DRAWN = 0x00961F88;
    // ParticleSystem: its particle storage; the manager: the particle step it ran last; a particle: velocity, position, next
    constexpr uintptr_t PARTICLE_STORAGE_OFFSET = 0xA4;
    constexpr uintptr_t PARTICLE_STEP_OFFSET = 0x74;
    constexpr uintptr_t PARTICLE_VELOCITY_OFFSET = 0x10;
    constexpr uintptr_t PARTICLE_POSITION_OFFSET = 0x1C;
    constexpr uintptr_t PARTICLE_NEXT_OFFSET = 0x64;
    // a particle step, render_rate's: two sub-frames
    constexpr int PARTICLE_STEP_SUB_FRAMES = 2;
    // GameEngine: the sub-frame, the sub-frames of a logic frame, the alpha; and its stock alpha, clamp(sub-frame / wrap)
    constexpr uintptr_t SUB_FRAME_OFFSET = 0x34;
    constexpr uintptr_t WRAP_OFFSET = 0x38;
    constexpr uintptr_t ALPHA_OFFSET = 0x3C;
    constexpr uintptr_t STOCK_ALPHA = 0x0063256F;

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
    // the stock game's frame, what one camera step is made for
    constexpr double STOCK_FRAME_MS = 2 * SUB_FRAME_MS;
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

    // how far a frame that only draws is on from the last sub-frame to the next: 0 to 1
    double progress(const int64_t at, const int64_t lastAdvance, const int64_t interval) {
        if (lastAdvance == 0 || interval <= 0 || at <= lastAdvance) {
            return lastAdvance == 0 ? 1.0 : 0.0;
        }
        const double share = static_cast<double>(at - lastAdvance) / static_cast<double>(interval);
        return share < 1.0 ? share : 1.0;
    }

    // The alpha a frame that only draws shows, `frac` of the way to the next sub-frame: the stock alpha (clamp(sub-frame / wrap))
    // one sub-frame back, moved on by `frac` of a sub-frame. At sub-frame 1 that starts from 0, where the logic frame before ended.
    float shownAlpha(const int32_t subFrame, const int32_t wrap, const double frac) {
        if (wrap <= 0) {
            return 0.0f;
        }
        const double alpha = (subFrame - 1 + frac) / wrap;
        return static_cast<float>(alpha < 0.0 ? 0.0 : alpha > 1.0 ? 1.0 : alpha);
    }

    // The W3D clock a frame shows: the stock clock's last step, `frac` of the way. The stock clock steps once per client frame,
    // on the first draw after a sub-frame; shown one step behind it moves on smoothly, never back (readers subtract unsigned), and
    // is the stock value at frac 1. A stock clock that goes back (a reset) is shown as it is.
    struct SyncClock {
        uint32_t base = 0;
        uint32_t stock = 0;
        bool known = false;

        uint32_t shown(const uint32_t now, const double frac, const uint32_t current) {
            if (!known || now < stock) {
                base = now;
                stock = now;
                known = true;
                return now;
            }
            if (now != stock) {
                base = stock;
                stock = now;
            }
            if (frac >= 1.0) {
                return now;
            }
            const auto moved = base + static_cast<uint32_t>(static_cast<double>(now - base) * frac);
            return current <= now && moved < current ? current : moved;
        }
    };

    uint8_t *engine = nullptr;
    int64_t lastAdvanceAt = 0;
    // this pass's share of the way to the next sub-frame, 1 on a pass that advances
    double passProgress = 1.0;
    bool alphaMoved = false;
    SyncClock syncClock;
    // the drawn frames, the drawable transform cache's stamp (never -1, the cache's "invalid")
    uint32_t drawnFrames = 1;

    // The tilts computed in a client frame, by drawable: a frame that only draws gets the one computed in its client frame. A
    // drawable whose slot another has taken is drawn level on such a frame, as without this.
    struct Tilts {
        static constexpr uint32_t SLOTS = 4096;

        struct Tilt {
            const void *drawable;
            uint32_t clientFrame;
            float values[4];
        };

        Tilt slots[SLOTS] = {};

        static uint32_t slotOf(const void *drawable) {
            return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(drawable)) >> 4) & (SLOTS - 1);
        }

        void keep(const void *drawable, const uint32_t frame, const float *values) {
            Tilt &tilt = slots[slotOf(drawable)];
            tilt.drawable = drawable;
            tilt.clientFrame = frame;
            for (int i = 0; i < 4; i++) {
                tilt.values[i] = values[i];
            }
        }

        bool again(const void *drawable, const uint32_t frame, float *values) const {
            const Tilt &tilt = slots[slotOf(drawable)];
            if (tilt.drawable != drawable || tilt.clientFrame != frame) {
                return false;
            }
            for (int i = 0; i < 4; i++) {
                values[i] = tilt.values[i];
            }
            return true;
        }
    };

    Tilts tilts;

    // The particles' positions before the last step, by particle.
    struct ParticlePositions {
        static constexpr uint32_t SLOTS = 16384;

        struct Kept {
            const void *particle;
            uint32_t step;
            float position[3];
        };

        Kept slots[SLOTS] = {};

        static uint32_t slotOf(const void *particle) {
            return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(particle)) >> 4) & (SLOTS - 1);
        }

        void keep(const void *particle, const uint32_t step, const float *position) {
            Kept &kept = slots[slotOf(particle)];
            kept.particle = particle;
            kept.step = step;
            kept.position[0] = position[0];
            kept.position[1] = position[1];
            kept.position[2] = position[2];
        }

        // where a particle is drawn `share` of the way from its kept position to `position`; false: where it is
        bool place(const void *particle, const uint32_t step, const float *position, const float *velocity, const double share,
                   float *placed) const {
            const Kept &kept = slots[slotOf(particle)];
            if (kept.particle != particle || kept.step != step || share >= 1.0) {
                return false;
            }
            double moved = 0;
            double speed = 0;
            for (int i = 0; i < 3; i++) {
                moved += (position[i] - kept.position[i]) * (position[i] - kept.position[i]);
                speed += velocity[i] * velocity[i];
            }
            // a step moves a particle by its velocity and a little drift: further, it is a new particle at a reused address
            if (moved > 4.0 * speed + 1.0) {
                return false;
            }
            for (int i = 0; i < 3; i++) {
                placed[i] = static_cast<float>(kept.position[i] + (position[i] - kept.position[i]) * share);
            }
            return true;
        }
    };

    ParticlePositions *particlePositions = nullptr;
    uint32_t particleStep = 0xFFFFFFFF;
    int64_t particleStepAt = 0;
    // the XMM registers around the draw cave's call (the loop's state lives in registers the cave does not see)
    alignas(16) uint8_t savedXmm[8 * 16];

    // The camera's step for the frame being drawn, as a share of a stock frame: the time since the frame before, so that where
    // the camera is follows the clock even when frames alternate between long ones (a sub-frame) and short ones (equal steps
    // then stutter: a long frame's picture shows as far a move as a short one's). After a stall (a load, new ground in a pan) the
    // usual frame time instead, the mean of the last frames near their median, so the camera does not jump. Times in ticks.
    struct StepScale {
        static constexpr int FRAMES = 12;
        static constexpr int KNOWN = 4;
        // longer than this is a stall, in stock frames
        static constexpr int STALL = 4;

        int64_t frames[FRAMES] = {};
        int count = 0;
        int index = 0;
        // render_rate's until enough frames are known
        float usual = 0.5f;

        // `took`: the time since the frame before
        float add(const int64_t took, const int64_t stockFrame) {
            if (took <= 0 || took >= STALL * stockFrame) {
                return usual;
            }
            frames[index] = took;
            index = (index + 1) % FRAMES;
            if (count < FRAMES) {
                count++;
            }
            if (count < KNOWN) {
                return usual;
            }
            int64_t sorted[FRAMES];
            for (int i = 0; i < count; i++) {
                int j = i;
                while (j > 0 && sorted[j - 1] > frames[i]) {
                    sorted[j] = sorted[j - 1];
                    j--;
                }
                sorted[j] = frames[i];
            }
            const double median = count % 2 != 0
                                      ? static_cast<double>(sorted[count / 2])
                                      : (static_cast<double>(sorted[count / 2 - 1]) + static_cast<double>(sorted[count / 2])) / 2;
            double sum = 0;
            int counted = 0;
            for (int i = 0; i < count; i++) {
                const auto frame = static_cast<double>(sorted[i]);
                if (frame >= 0.55 * median && frame <= 1.5 * median) {
                    sum += frame;
                    counted++;
                }
            }
            usual = static_cast<float>((counted > 0 ? sum / counted : median) / static_cast<double>(stockFrame));
            return static_cast<float>(static_cast<double>(took) / static_cast<double>(stockFrame));
        }
    };

    StepScale stepScale;
    float *cameraScale = nullptr;
    int64_t lastFrameAt = 0;

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

    // the camera steps in this pass's client update, after any wait
    void stepCamera(const int64_t at) {
        const auto stockFrame = static_cast<int64_t>(STOCK_FRAME_MS * static_cast<double>(ticksPerSecond) / 1000.0);
        int64_t paced = 0;
        if (picture_pacer::schedule(at, paced)) {
            *cameraScale = stepScale.add(paced, stockFrame);
        } else if (lastFrameAt != 0) {
            *cameraScale = stepScale.add(at - lastFrameAt, stockFrame);
        }
        lastFrameAt = at;
#if GAME_PATCHER_FRAME_PACING_TRACE
        instrumentation::frame_trace::pass(at, *cameraScale, passAdvances);
#endif
    }

    // what this pass's draw shows of the time between the sub-frames: the alpha moved on, or the stock one back on a pass that
    // advances (its draw comes before the dispatch, which computes it again for the logic)
    void showProgress(const bool advances) {
        drawnFrames = drawnFrames + 1 == 0xFFFFFFFF ? 1 : drawnFrames + 1;
        if (engine == nullptr) {
            passProgress = 1.0;
            return;
        }
        if (advances) {
            passProgress = 1.0;
            if (alphaMoved) {
                reinterpret_cast<Pass>(STOCK_ALPHA)(engine);
                alphaMoved = false;
            }
            return;
        }
        passProgress = progress(passAt, lastAdvanceAt, subFrameTicks());
        *reinterpret_cast<float *>(engine + ALPHA_OFFSET) = shownAlpha(*reinterpret_cast<const int32_t *>(engine + SUB_FRAME_OFFSET),
                                                                      *reinterpret_cast<const int32_t *>(engine + WRAP_OFFSET), passProgress);
        alphaMoved = true;
    }

    bool __fastcall frameOnlyDraws(void *scriptEngine) {
        passAt = now();
        if (reinterpret_cast<Predicate>(FRAME_ONLY_DRAWS)(scriptEngine)) {
            // the script debugger's pause: the picture stands
            passAdvances = false;
            showProgress(true);
            stepCamera(passAt);
            return true;
        }
        const Pacer::Decision decision = pacer.decide(passAt, *reinterpret_cast<const uint8_t *>(LIMITER_ON) != 0, subFrameTicks());
        if (decision == Pacer::WAIT_THEN_ADVANCE) {
            waitUntil(pacer.next);
            passAt = now();
        }
        passAdvances = decision != Pacer::ONLY_DRAW;
        showProgress(passAdvances);
        stepCamera(passAt);
        return !passAdvances;
    }

    void __fastcall enginePass(void *gameEngine) {
        engine = static_cast<uint8_t *>(gameEngine);
        reinterpret_cast<Pass>(ENGINE_PASS)(gameEngine);
        if (!passAdvances) {
            return;
        }
        const uint8_t *client = gameClient();
        const bool ran = client != nullptr && client[TIME_RAN_OFFSET] != 0;
        if (ran) {
            lastAdvanceAt = passAt;
        }
        pacer.advanced(passAt, ran, subFrameTicks());
    }

    // the stock W3D clock (the accumulator) in, cdecl as the setter
    void __cdecl setSyncTime(const uint32_t stock) {
        const uint32_t shown = syncClock.shown(stock, passProgress, *reinterpret_cast<const uint32_t *>(SYNC_TIME));
        reinterpret_cast<void (__cdecl *)(uint32_t)>(SYNC_TIME_SET)(shown);
    }

    // Drawable::calcPhysicsXform(info): thiscall, the four floats of the tilt; false: nothing to apply
    bool __fastcall physicsXform(void *drawable, void *, float *info) {
        const uint32_t frame = clientFrame();
        if (reinterpret_cast<bool (__thiscall *)(void *, float *)>(PHYSICS_XFORM)(drawable, info)) {
            tilts.keep(drawable, frame, info);
            return true;
        }
        return tilts.again(drawable, frame, info);
    }

    // before the manager updates a particle system: its particles' positions, for the step about to run
    void __stdcall keepParticles(uint8_t *system, const uint8_t *manager) {
        if (system == nullptr) {
            return;
        }
        const uint32_t step = *reinterpret_cast<const uint32_t *>(manager + PARTICLE_STEP_OFFSET);
        if (step != particleStep) {
            particleStep = step;
            particleStepAt = now();
        }
        auto *storage = *reinterpret_cast<uint8_t **>(system + PARTICLE_STORAGE_OFFSET);
        if (storage == nullptr || particlePositions == nullptr) {
            return;
        }
        using First = uint8_t *(__thiscall *)(void *);
        const auto first = (*reinterpret_cast<First *const *>(storage))[0x20 / 4];
        for (uint8_t *particle = first(storage); particle != nullptr; particle = *reinterpret_cast<uint8_t **>(particle + PARTICLE_NEXT_OFFSET)) {
            particlePositions->keep(particle, step, reinterpret_cast<const float *>(particle + PARTICLE_POSITION_OFFSET));
        }
    }

    // the default particle draw: the particle's position into the point array (x, y, z at destination - 8, - 4, + 0)
    void __stdcall placeParticle(const uint8_t *particle, float *destination) {
        const auto *position = reinterpret_cast<const float *>(particle + PARTICLE_POSITION_OFFSET);
        float placed[3];
        const double share = progress(passAt, particleStepAt, PARTICLE_STEP_SUB_FRAMES * subFrameTicks());
        if (particlePositions == nullptr ||
            !particlePositions->place(particle, particleStep, position, reinterpret_cast<const float *>(particle + PARTICLE_VELOCITY_OFFSET),
                                      share, placed)) {
            placed[0] = position[0];
            placed[1] = position[1];
            placed[2] = position[2];
        }
        destination[-2] = placed[0];
        destination[-1] = placed[1];
        destination[0] = placed[2];
    }

    void addParticleKeep(PatchSet &set, Cave &cave) {
        const uintptr_t entry = cave.here();
        const BYTE system[3] = {0x8B, 0x4D, 0xCC}; // mov ecx, [ebp - 0x34]: the system
        cave.emit(system);
        cave.emit8(0x60); // pushad
        cave.emit8(0x57); // push edi: the manager
        cave.emit8(0x51); // push ecx: the system
        cave.call(reinterpret_cast<uintptr_t>(&keepParticles));
        cave.emit8(0x61); // popad
        const BYTE check[2] = {0x3B, 0xCB}; // cmp ecx, ebx: the null check the je behind reads
        cave.emit(check);
        cave.jmp(PARTICLE_SYSTEM_CHECKED);
        const BYTE stock[5] = {0x8B, 0x4D, 0xCC, 0x3B, 0xCB};
        set.hook(PARTICLE_SYSTEM_TAKEN, stock, entry);
        // je past the update; push [edi + 0x64] / mov eax, [ecx] / call [eax + 0x10]: the system's update, with the manager in edi
        const BYTE nullCheck[2] = {0x74, 0x36};
        set.expect(PARTICLE_SYSTEM_CHECKED, nullCheck);
        const BYTE update[8] = {0xFF, 0x77, 0x64, 0x8B, 0x01, 0xFF, 0x50, 0x10};
        set.expect(0x005F51E6, update);
        // mov [edi + 0x74], eax: the manager's particle step, stored by render_rate's gate
        const BYTE stepStore[3] = {0x89, 0x47, 0x74};
        set.expect(0x005F518C, stepStore);
    }

    void addParticlePlace(PatchSet &set, Cave &cave) {
        const uintptr_t entry = cave.here();
        for (BYTE xmm = 0; xmm < 8; xmm++) {
            const BYTE save[3] = {0x0F, 0x11, static_cast<BYTE>(0x05 | (xmm << 3))}; // movups [savedXmm + 16 * xmm], xmm
            cave.emit(save);
            cave.emit32(address(savedXmm + 16 * xmm));
        }
        cave.emit8(0x60); // pushad
        cave.emit8(0x57); // push edi: the destination
        cave.emit8(0x56); // push esi: the particle
        cave.call(reinterpret_cast<uintptr_t>(&placeParticle));
        cave.emit8(0x61); // popad
        for (BYTE xmm = 0; xmm < 8; xmm++) {
            const BYTE load[3] = {0x0F, 0x10, static_cast<BYTE>(0x05 | (xmm << 3))}; // movups xmm, [savedXmm + 16 * xmm]
            cave.emit(load);
            cave.emit32(address(savedXmm + 16 * xmm));
        }
        const BYTE counter[3] = {0x8B, 0x4D, 0xF0}; // mov ecx, [ebp - 0x10]: as the displaced code leaves it
        cave.emit(counter);
        cave.jmp(PARTICLE_DRAWN);

        // mov eax, [esi + 0x1C] / mov ecx, [ebp - 0x10] / mov [edi - 8], eax / mov eax, [esi + 0x20] / mov [edi - 4], eax /
        // mov eax, [esi + 0x24] / mov [edi], eax
        const BYTE stock[20] = {0x8B, 0x46, 0x1C, 0x8B, 0x4D, 0xF0, 0x89, 0x47, 0xF8, 0x8B, 0x46, 0x20, 0x89, 0x47, 0xFC, 0x8B, 0x46, 0x24,
                                0x89, 0x07};
        set.hook(PARTICLE_DRAW, stock, entry);
        // mov eax, [ebp - 0x38] / movss [eax + ecx * 4], xmm0: the size, behind
        const BYTE size[8] = {0x8B, 0x45, 0xC8, 0xF3, 0x0F, 0x11, 0x04, 0x88};
        set.expect(PARTICLE_DRAWN, size);
        // mov eax, [ecx] / call [eax + 0x20] (the storage's first particle) ... mov esi, [esi + 0x64]: the list the draw walks
        const BYTE firstParticle[5] = {0x8B, 0x01, 0xFF, 0x50, 0x20};
        set.expect(0x00961EC4, firstParticle);
        const BYTE nextParticle[3] = {0x8B, 0x76, 0x64};
        set.expect(0x00961FF5, nextParticle);
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
    cameraScale = renderRateScrollScale();
    if (cameraScale == nullptr) {
        return false;
    }

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

    const BYTE syncTimeSetCall[5] = {0xE8, 0xF6, 0xB4, 0x0C, 0x00};
    set.redirectCall(SYNC_TIME_SET_CALL, syncTimeSetCall, reinterpret_cast<uintptr_t>(&setSyncTime));
    // push [accumulator] in front, pop ecx behind (cdecl); timeGetTime follows on both ways, so edx is free
    const BYTE pushAccumulator[6] = {0xFF, 0x35, 0x80, 0x75, 0xDC, 0x00};
    set.expect(SYNC_TIME_SET_CALL - 6, pushAccumulator);
    const BYTE popArgument[8] = {0xF6, 0x05, 0x04, 0x77, 0xDC, 0x00, 0x01, 0x59};
    set.expect(SYNC_TIME_SET_CALL + 5, popArgument);
    // mov eax, [SyncTime] / mov ecx, [esp + 4] / mov [PreviousSyncTime], eax / mov [SyncTime], ecx / ret
    const BYTE setter[21] = {0xA1, 0x0C, 0x1E, 0xDD, 0x00, 0x8B, 0x4C, 0x24, 0x04, 0xA3, 0x10, 0x1E, 0xDD, 0x00, 0x89, 0x0D, 0x0C, 0x1E,
                             0xDD, 0x00, 0xC3};
    set.expect(SYNC_TIME_SET, setter);

    // mov ecx, [TheGameClient] / mov eax, [ecx] / push ebx / call [eax + 0x7C] (getFrame) -> push ebx / mov eax, [drawnFrames]
    const BYTE stampStock[12] = {0x8B, 0x0D, 0x88, 0x43, 0xDE, 0x00, 0x8B, 0x01, 0x53, 0xFF, 0x50, 0x7C};
    BYTE stampDrawn[12] = {0x53, 0xA1, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    putInt(stampDrawn + 2, address(&drawnFrames));
    set.replace(TRANSFORM_STAMP, stampStock, stampDrawn);
    // mov ebx, eax / cmp [esi + 0x244], ebx: the stamp compared, and stored at 0x006766DD
    const BYTE stampCompare[8] = {0x8B, 0xD8, 0x39, 0x9E, 0x44, 0x02, 0x00, 0x00};
    set.expect(TRANSFORM_STAMP + 12, stampCompare);
    const BYTE stampStore[6] = {0x89, 0x9E, 0x44, 0x02, 0x00, 0x00};
    set.expect(0x006766DD, stampStore);

    // cvtsi2ss xmm1, [ecx + 0x38] / cvtsi2ss xmm0, [ecx + 0x34] / divss xmm0, xmm1: the stock alpha, called on a pass that advances
    const BYTE stockAlpha[14] = {0xF3, 0x0F, 0x2A, 0x49, 0x38, 0xF3, 0x0F, 0x2A, 0x41, 0x34, 0xF3, 0x0F, 0x5E, 0xC1};
    set.expect(STOCK_ALPHA, stockAlpha);

    const BYTE physicsXformCall[5] = {0xE8, 0x82, 0xFE, 0xFF, 0xFF};
    set.redirectCall(PHYSICS_XFORM_CALL, physicsXformCall, reinterpret_cast<uintptr_t>(&physicsXform));
    // lea eax, [ebp - 0x14] / push eax / mov ecx, esi in front; test al, al behind: the tilt's floats, the drawable, the answer
    const BYTE tiltArguments[6] = {0x8D, 0x45, 0xEC, 0x50, 0x8B, 0xCE};
    set.expect(0x0067BF1F, tiltArguments);
    const BYTE tiltAnswer[2] = {0x84, 0xC0};
    set.expect(PHYSICS_XFORM_CALL + 5, tiltAnswer);
    // the refusal within a client frame: call [eax + 0x7C] (getFrame) / mov ebx, eax / cmp [esi + 0xAC], ebx
    const BYTE tiltStamp[11] = {0xFF, 0x50, 0x7C, 0x8B, 0xD8, 0x39, 0x9E, 0xAC, 0x00, 0x00, 0x00};
    set.expect(0x0067BDFF, tiltStamp);

    particlePositions = static_cast<ParticlePositions *>(VirtualAlloc(nullptr, sizeof(ParticlePositions), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (particlePositions == nullptr) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    addParticleKeep(set, cave);
    addParticlePlace(set, cave);

    addSlowdownGate(set, cave);
    addLimiterPeriod(set, cave);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        VirtualFree(particlePositions, 0, MEM_RELEASE);
        particlePositions = nullptr;
        return false;
    }
    return true;
}
