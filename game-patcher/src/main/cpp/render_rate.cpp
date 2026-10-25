#include <windows.h>
#include <cstdint>
#include <cstring>

#include "patch_set.h"
#include "render_rate.h"

// Lets the game draw at 60 frames per second without changing its pace. Derived from the render-rate patch of pySAGE
// (https://github.com/ClementJ18/pySAGE), but applied to the process image instead of the file and with a different
// frame limiter, see the comments below.
//
// Every site is verified before anything is written, and the patch is applied as a whole or not at all: a half moved
// set of constants gives a game that runs at the wrong speed instead of one that crashes.

using namespace patching;

namespace {

    constexpr uintptr_t LOGIC_RATE_ADDRESS = 0x00D9F608;
    constexpr uintptr_t CLIENT_RATE_ADDRESS = 0x00D9F60C;
    constexpr uintptr_t SLOWDOWN_SCALAR_ADDRESS = 0x00D9F498;
    constexpr uintptr_t LATCH_DIVISOR_ADDRESS = 0x00ECA400;
    constexpr uintptr_t GAME_CLIENT_ADDRESS = 0x00DE4388;
    constexpr uintptr_t FTOL_ADDRESS = 0x00A3CFA4;
    constexpr uintptr_t GLOBAL_DATA_ADDRESS = 0x00DE4364;

    constexpr int LOGIC_RATE = 5;
    constexpr int STOCK_CLIENT_RATE = 30;
    constexpr int CLIENT_RATE = 60;

    // A logic frame ends when the sub-frame counter passes this value. The catch-up loop at 0x00632A9B steps the
    // counter past 1 inside the frame that wraps it, so a logic frame spans (WRAP - 1) client frames. The stock game
    // wraps at 6 and draws a frame every 33 ms, i.e. 5 * 33 ms = 165 ms per logic frame. 10 * 16.5 ms keeps that.
    constexpr BYTE STOCK_WRAP = 6;
    constexpr BYTE WRAP = 11;

    // The frame limiter alternates between these two periods, which averages to the 16.5 ms mentioned above. The
    // truncating conversion of the stock limiter cannot express that with a single integer period.
    constexpr BYTE FRAME_PERIOD_MS = 16;

    // Run time state of the caves, kept in front of their code.
    struct CaveData {
        uint32_t frameCounter;
        uint32_t spellStoreTick;
        uint32_t getTickCount;
        float scrollScale;
        uint32_t savedScrollFactors[4];
    };

    void addRate(PatchSet &set) {
        BYTE expected[4];
        BYTE replacement[4];

        putInt(expected, STOCK_CLIENT_RATE);
        putInt(replacement, CLIENT_RATE);
        set.replace(CLIENT_RATE_ADDRESS, expected, replacement);

        putFloat(expected, 1000.0 / STOCK_CLIENT_RATE);
        putFloat(replacement, 1000.0 / CLIENT_RATE);
        set.replace(0x00D9F620, expected, replacement);

        putFloat(expected, STOCK_CLIENT_RATE * 0.001);
        putFloat(replacement, CLIENT_RATE * 0.001);
        set.replace(0x00D9F624, expected, replacement);

        putFloat(expected, STOCK_CLIENT_RATE);
        putFloat(replacement, CLIENT_RATE);
        set.replace(0x00D9F628, expected, replacement);

        putFloat(expected, 1.0 / STOCK_CLIENT_RATE);
        putFloat(replacement, 1.0 / CLIENT_RATE);
        set.replace(0x00D9F62C, expected, replacement);

        // the once per logic frame latch answers "sub-frame == 6 / (clientRate / divisor)", which has to stay on the
        // second sub-frame because the first one is never visible to client code
        putInt(expected, 8);
        putInt(replacement, CLIENT_RATE / 3);
        set.replace(LATCH_DIVISOR_ADDRESS, expected, replacement);

        const BYTE logicRate[4] = {LOGIC_RATE, 0x00, 0x00, 0x00};
        set.expect(LOGIC_RATE_ADDRESS, logicRate);
    }

    void addWrap(PatchSet &set) {
        const BYTE stockWrap[3] = {0x83, 0xF8, STOCK_WRAP};
        const BYTE wrap[3] = {0x83, 0xF8, WRAP};
        set.replace(0x0063264A, stockWrap, wrap);

        // keeps the relationship the stock game has between gate and wrap
        const BYTE stockGate[3] = {0x83, 0xF9, STOCK_WRAP};
        const BYTE gate[3] = {0x83, 0xF9, WRAP};
        set.replace(0x00632604, stockGate, gate);

        // the ratio the interpolation alpha is divided by must be the wrap, not clientRate / logicRate
        const BYTE stockRatio[12] = {
            0xA1, 0x0C, 0xF6, 0xD9, 0x00, // mov eax, [clientRate]
            0x99,                         // cdq
            0xF7, 0x3D, 0x08, 0xF6, 0xD9, 0x00 // idiv [logicRate]
        };
        const BYTE ratio[12] = {
            0xB8, WRAP, 0x00, 0x00, 0x00, // mov eax, WRAP
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
        };
        set.replace(0x0063260F, stockRatio, ratio);

        // timecode strides, the room one logic frame has for its sub-frame indices
        const BYTE stockStrideEax[3] = {0x6B, 0xC0, 0x0A};
        const BYTE strideEax[3] = {0x6B, 0xC0, WRAP};
        set.replace(0x0062ECC6, stockStrideEax, strideEax);

        const BYTE stockStrideEcx[3] = {0x6B, 0xC9, 0x0A};
        const BYTE strideEcx[3] = {0x6B, 0xC9, WRAP};
        set.replace(0x00632631, stockStrideEcx, strideEcx);
        set.replace(0x00632ACF, stockStrideEcx, strideEcx);

        // the catch-up loop that makes a logic frame span WRAP - 1 client frames
        const BYTE catchUp[7] = {0xB8, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x19};
        set.expect(0x00632A9B, catchUp);
    }

    // GPU particles age by milliseconds * clientRate, but their content is authored for 30 updates per second
    void addGpuParticleClocks(PatchSet &set) {
        const BYTE stock[7] = {0x0F, 0xAF, 0x05, 0x0C, 0xF6, 0xD9, 0x00}; // imul eax, [clientRate]
        const BYTE patched[7] = {0x6B, 0xC0, STOCK_CLIENT_RATE, 0x90, 0x90, 0x90, 0x90}; // imul eax, eax, 30
        set.replace(0x007B1102, stock, patched);
        set.replace(0x007B1160, stock, patched);
        set.replace(0x007B12FE, stock, patched);
        set.replace(0x007B140B, stock, patched);
    }

    // CPU particles step once per client frame, i.e. twice as fast as authored. Stamps the manager with
    // clientFrame * 30 / clientRate instead and lets the engine compare and store it.
    void addParticleGate(PatchSet &set, Cave &cave) {
        const uintptr_t entry = cave.here();
        const uintptr_t resume = 0x005F5183;
        const uintptr_t run = 0x005F518F;

        const BYTE loadClient[2] = {0x8B, 0x0D}; // mov ecx, [TheGameClient]
        cave.emit(loadClient);
        cave.emit32(GAME_CLIENT_ADDRESS);
        const BYTE testClient[2] = {0x85, 0xC9}; // test ecx, ecx
        cave.emit(testClient);
        const size_t jumpToRun = cave.size;
        cave.jcc(JE, 0); // patched below
        const BYTE getFrame[7] = {
            0x8B, 0x01,       // mov eax, [ecx]
            0xFF, 0x50, 0x7C, // call [eax + 0x7C]   GameClient::getFrame
            0x6B, 0xC0        // imul eax, eax, ...
        };
        cave.emit(getFrame);
        cave.emit8(STOCK_CLIENT_RATE);
        const BYTE divide[2] = {0x33, 0xD2}; // xor edx, edx
        cave.emit(divide);
        const BYTE divideByRate[2] = {0xF7, 0x35}; // div dword [clientRate]
        cave.emit(divideByRate);
        cave.emit32(CLIENT_RATE_ADDRESS);
        cave.jmp(resume);

        const uintptr_t runLabel = cave.here();
        cave.jmp(run);

        // resolve the je above
        const auto displacement = static_cast<uint32_t>(runLabel - (reinterpret_cast<uintptr_t>(cave.base) + jumpToRun + 6));
        memcpy(cave.base + jumpToRun + 2, &displacement, sizeof(displacement));

        const BYTE stock[17] = {
            0xA1, 0x64, 0x43, 0xDE, 0x00, 0x3B, 0xC3, 0x74, 0x27, 0x38, 0x98, 0x45, 0x0D, 0x00, 0x00, 0x74, 0x1F
        };
        set.hook(0x005F515F, stock, entry);

        const BYTE resumeBytes[12] = {0x3B, 0x47, 0x74, 0x0F, 0x84, 0xD0, 0x00, 0x00, 0x00, 0x89, 0x47, 0x74};
        set.expect(resume, resumeBytes);
        const BYTE runBytes[3] = {0x89, 0x5D, 0xCC};
        set.expect(run, runBytes);
    }

    // The spell store sends the state of each button once and never confirms that the movie accepted it. A faster
    // client lets the movie refuse the first send, which leaves the spell book empty. Invalidates the cached states
    // when the panel is initialised and holds the sends back for half a second.
    void addSpellStore(PatchSet &set, Cave &cave, const CaveData *data) {
        constexpr uintptr_t STATES_OFFSET = 0x2AC;
        constexpr uintptr_t STATES_END_OFFSET = 0x34C;
        constexpr uint32_t DEFER_MS = 500;

        const uintptr_t tickAddress = reinterpret_cast<uintptr_t>(&data->spellStoreTick);
        const uintptr_t getTickCountAddress = reinterpret_cast<uintptr_t>(&data->getTickCount);

        const uintptr_t resetEntry = cave.here();
        const BYTE enableStore[7] = {0xC6, 0x81, 0xA0, 0x02, 0x00, 0x00, 0x01}; // mov byte [ecx + 0x2A0], 1
        cave.emit(enableStore);
        cave.emit8(0x51); // push ecx
        const BYTE callGetTickCount[2] = {0xFF, 0x15};
        cave.emit(callGetTickCount);
        cave.emit32(getTickCountAddress);
        cave.emit8(0x59); // pop ecx
        cave.emit8(0xA3); // mov [tick], eax
        cave.emit32(tickAddress);
        const BYTE sentinel[3] = {0x83, 0xC8, 0xFF}; // or eax, -1
        cave.emit(sentinel);
        const BYTE firstState[2] = {0x8D, 0x91}; // lea edx, [ecx + 0x2AC]
        cave.emit(firstState);
        cave.emit32(STATES_OFFSET);
        const BYTE endOfStates[2] = {0x8D, 0x89}; // lea ecx, [ecx + 0x34C]
        cave.emit(endOfStates);
        cave.emit32(STATES_END_OFFSET);
        const uintptr_t next = cave.here();
        const BYTE invalidate[7] = {
            0x89, 0x02,       // mov [edx], eax
            0x83, 0xC2, 0x08, // add edx, 8
            0x3B, 0xD1        // cmp edx, ecx
        };
        cave.emit(invalidate);
        cave.jcc(JB, next);
        const uintptr_t resetResume = 0x00822897;
        cave.jmp(resetResume);

        const BYTE stockReset[7] = {0xC6, 0x81, 0xA0, 0x02, 0x00, 0x00, 0x01};
        set.hook(0x00822890, stockReset, resetEntry);
        const BYTE resetLanding[3] = {0xC2, 0x04, 0x00};
        set.expect(resetResume, resetLanding);

        const uintptr_t deferEntry = cave.here();
        cave.emit(callGetTickCount);
        cave.emit32(getTickCountAddress);
        const BYTE elapsed[2] = {0x2B, 0x05}; // sub eax, [tick]
        cave.emit(elapsed);
        cave.emit32(tickAddress);
        cave.emit8(0x3D); // cmp eax, DEFER_MS
        cave.emit32(DEFER_MS);
        const size_t jumpToDefer = cave.size;
        cave.jcc(JB, 0); // patched below
        const BYTE stockLoopHead[10] = {0x83, 0x65, 0xEC, 0x00, 0x8D, 0xBE, 0xAC, 0x02, 0x00, 0x00};
        cave.emit(stockLoopHead);
        const uintptr_t loopTop = 0x008231D4;
        cave.jmp(loopTop);
        const uintptr_t deferLabel = cave.here();
        const uintptr_t loopDone = 0x0082325E;
        cave.jmp(loopDone);

        const auto displacement = static_cast<uint32_t>(deferLabel - (reinterpret_cast<uintptr_t>(cave.base) + jumpToDefer + 6));
        memcpy(cave.base + jumpToDefer + 2, &displacement, sizeof(displacement));

        set.hook(0x008231CA, stockLoopHead, deferEntry);
        const BYTE loopTopBytes[3] = {0x8B, 0x47, 0xFC};
        set.expect(loopTop, loopTopBytes);
        const BYTE loopDoneBytes[6] = {0x8B, 0x4D, 0xF4, 0x5F, 0x5E, 0x5B};
        set.expect(loopDone, loopDoneBytes);
    }

    // The stock limiter waits for ftol(1000 / (maxFramesPerSecond * slowdown)) milliseconds, where maxFramesPerSecond
    // comes from the INI (30 in every archive that ships with the mod) and is written back from there at several
    // points. This takes the period from a fixed 16 / 17 ms pattern instead and leaves the rest of the limiter alone.
    void addFrameLimiter(PatchSet &set, Cave &cave, const CaveData *data) {
        const uintptr_t counterAddress = reinterpret_cast<uintptr_t>(&data->frameCounter);
        const uintptr_t entry = cave.here();
        const uintptr_t resume = 0x0063A1B2;

        const BYTE keepTime[2] = {0x8B, 0xF8}; // mov edi, eax   the timeGetTime result the displaced code kept
        cave.emit(keepTime);
        cave.emit8(0xA1); // mov eax, [counter]
        cave.emit32(counterAddress);
        cave.emit8(0x40); // inc eax
        cave.emit8(0xA3); // mov [counter], eax
        cave.emit32(counterAddress);
        const BYTE alternate[3] = {0x83, 0xE0, 0x01}; // and eax, 1
        cave.emit(alternate);
        const BYTE period[3] = {0x83, 0xC0, FRAME_PERIOD_MS}; // add eax, FRAME_PERIOD_MS
        cave.emit(period);
        cave.emit8(0x50); // push eax
        const BYTE loadPeriod[3] = {0xDB, 0x04, 0x24}; // fild dword [esp]
        cave.emit(loadPeriod);
        cave.emit8(0x59); // pop ecx
        const BYTE scale[2] = {0xD8, 0x35}; // fdiv dword [slowdown]
        cave.emit(scale);
        cave.emit32(SLOWDOWN_SCALAR_ADDRESS);
        cave.call(FTOL_ADDRESS);
        cave.jmp(resume);

        const BYTE stock[22] = {
            0xDB, 0x46, 0x0C,                   // fild dword [esi + 0xC]
            0x8B, 0xF8,                         // mov edi, eax
            0xD8, 0x0D, 0x98, 0xF4, 0xD9, 0x00, // fmul dword [slowdown]
            0xD8, 0x3D, 0x88, 0x43, 0xBD, 0x00, // fdivr dword [1000.0f]
            0xE8, 0xF2, 0x2D, 0x40, 0x00        // call ftol
        };
        set.hook(0x0063A19C, stock, entry);

        const BYTE timeGetTimeCall[6] = {0xFF, 0x15, 0x20, 0x09, 0xBD, 0x00};
        set.expect(0x0063A196, timeGetTimeCall);
        const BYTE landing[2] = {0x8B, 0xF0}; // mov esi, eax
        set.expect(resume, landing);
    }

    // Emits a stub that calls `target` with KeyboardScrollSpeedFactor and KeyboardCameraRotateSpeed scaled down for the
    // duration of the call and restored afterwards. `ecx` (this) is left alone.
    uintptr_t emitScaledCall(Cave &cave, const CaveData *data, const size_t slot, const uintptr_t target) {
        constexpr uint32_t KEYBOARD_SCROLL_SPEED_FACTOR = 0xAF8;
        constexpr uint32_t KEYBOARD_CAMERA_ROTATE_SPEED = 0xC2C;

        const uintptr_t savedScroll = reinterpret_cast<uintptr_t>(&data->savedScrollFactors[slot * 2]);
        const uintptr_t savedRotate = savedScroll + sizeof(uint32_t);
        const uintptr_t scale = reinterpret_cast<uintptr_t>(&data->scrollScale);

        const uintptr_t entry = cave.here();
        cave.emit8(0x53); // push ebx
        const BYTE loadGlobalData[2] = {0x8B, 0x1D}; // mov ebx, [TheGlobalData]
        cave.emit(loadGlobalData);
        cave.emit32(GLOBAL_DATA_ADDRESS);
        const BYTE testGlobalData[2] = {0x85, 0xDB}; // test ebx, ebx
        cave.emit(testGlobalData);
        const size_t jumpToUnscaled = cave.size;
        cave.jcc(JE, 0); // patched below

        const BYTE loadField[2] = {0x8B, 0x93}; // mov edx, [ebx + field]
        const BYTE storeField[2] = {0x89, 0x93}; // mov [ebx + field], edx
        const BYTE loadSaved[2] = {0x8B, 0x15}; // mov edx, [saved]
        const BYTE storeSaved[2] = {0x89, 0x15}; // mov [saved], edx
        const BYTE loadFloat[4] = {0xF3, 0x0F, 0x10, 0x83}; // movss xmm0, [ebx + field]
        const BYTE storeFloat[4] = {0xF3, 0x0F, 0x11, 0x83}; // movss [ebx + field], xmm0
        const BYTE multiply[4] = {0xF3, 0x0F, 0x59, 0x05}; // mulss xmm0, [scale]

        cave.emit(loadField);
        cave.emit32(KEYBOARD_SCROLL_SPEED_FACTOR);
        cave.emit(storeSaved);
        cave.emit32(savedScroll);
        cave.emit(loadField);
        cave.emit32(KEYBOARD_CAMERA_ROTATE_SPEED);
        cave.emit(storeSaved);
        cave.emit32(savedRotate);

        cave.emit(loadFloat);
        cave.emit32(KEYBOARD_SCROLL_SPEED_FACTOR);
        cave.emit(multiply);
        cave.emit32(scale);
        cave.emit(storeFloat);
        cave.emit32(KEYBOARD_SCROLL_SPEED_FACTOR);
        cave.emit(loadFloat);
        cave.emit32(KEYBOARD_CAMERA_ROTATE_SPEED);
        cave.emit(multiply);
        cave.emit32(scale);
        cave.emit(storeFloat);
        cave.emit32(KEYBOARD_CAMERA_ROTATE_SPEED);

        cave.call(target);

        cave.emit(loadSaved);
        cave.emit32(savedScroll);
        cave.emit(storeField);
        cave.emit32(KEYBOARD_SCROLL_SPEED_FACTOR);
        cave.emit(loadSaved);
        cave.emit32(savedRotate);
        cave.emit(storeField);
        cave.emit32(KEYBOARD_CAMERA_ROTATE_SPEED);
        cave.emit8(0x5B); // pop ebx
        cave.emit8(0xC3); // ret

        const uintptr_t unscaled = cave.here();
        cave.call(target);
        cave.emit8(0x5B); // pop ebx
        cave.emit8(0xC3); // ret

        const auto displacement = static_cast<uint32_t>(unscaled - (reinterpret_cast<uintptr_t>(cave.base) + jumpToUnscaled + 6));
        memcpy(cave.base + jumpToUnscaled + 2, &displacement, sizeof(displacement));
        return entry;
    }

    // The camera scroll and rotation steps are applied once per client frame without a time base: the two factors are
    // the distance per call, so doubling the client rate doubles the scroll speed. Both subsystems are called once from
    // GameClient::update. 0x006A1F4D reads the same fields but divides by the client rate itself, which is why the
    // fields cannot simply be rescaled once.
    void addCameraScroll(PatchSet &set, Cave &cave, const CaveData *data) {
        const uintptr_t scrollEntry = emitScaledCall(cave, data, 0, 0x0083B471);
        const uintptr_t viewEntry = emitScaledCall(cave, data, 1, 0x008392A7);

        const BYTE scrollCall[5] = {0xE8, 0xBA, 0x2F, 0x1F, 0x00};
        set.redirectCall(0x006484B2, scrollCall, scrollEntry);
        const BYTE viewCall[5] = {0xE8, 0xE5, 0x0D, 0x1F, 0x00};
        set.redirectCall(0x006484BD, viewCall, viewEntry);
    }

}

bool applyRenderRatePatch() {

    const auto getTickCount = reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleA("kernel32"), "GetTickCount"));
    if (getTickCount == 0) {
        return false;
    }

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    auto *data = reinterpret_cast<CaveData *>(caveMemory);
    memset(data, 0, sizeof(CaveData));
    data->getTickCount = static_cast<uint32_t>(getTickCount);
    data->scrollScale = static_cast<float>(STOCK_CLIENT_RATE) / CLIENT_RATE;

    Cave cave(caveMemory);
    cave.size = sizeof(CaveData);

    static PatchSet set;
    set.reset();

    addRate(set);
    addWrap(set);
    addGpuParticleClocks(set);
    addParticleGate(set, cave);
    addSpellStore(set, cave, data);
    addFrameLimiter(set, cave, data);
    addCameraScroll(set, cave, data);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
