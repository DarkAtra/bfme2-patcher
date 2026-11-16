#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "instrumentation/logic_crc_log.h"
#include "patch_set.h"

// Logic CRC log (debug mode, with -logcrc on the game's command line)
//
// Writes the logic's CRC every LOG_INTERVAL logic frames to %TEMP%\bfme2-logic-crc.csv (frame, CRC), so that two runs of the same
// match (the auto skirmish with -fixedseed, fixed_seed.cpp) with different builds can be compared: a build that keeps the logic
// exact gives the same lines. The
// CRC is the one the network heartbeat (MSG_LOGIC_CRC) carries: GameLogic::getCRC (0x00625886, thiscall, argument 0) with the
// lite flag (0x00DE87C7) set around it, as GameLogic::update does at 0x0062E7E4. A skirmish never sends the heartbeat (game mode 2
// clears its gate), so it is computed here, at that gate (0x0062E75D), once per logic frame.

using namespace patching;

namespace {

    constexpr uintptr_t HEARTBEAT_GATE = 0x0062E75D;
    constexpr uintptr_t HEARTBEAT_SKIPPED = 0x0062E876;
    constexpr uintptr_t HEARTBEAT_SENT = 0x0062E765;
    constexpr uintptr_t GET_CRC = 0x00625886;
    constexpr uintptr_t LITE_CRC = 0x00DE87C7;
    constexpr uintptr_t LOGIC_FRAME_OFFSET = 0x40;
    constexpr uint32_t LOG_INTERVAL = 25;

    uint32_t lastFrame = 0xFFFFFFFF;
    FILE *file = nullptr;
    bool failed = false;

    void __stdcall logCrc(uint8_t *logic) {
        const uint32_t frame = *reinterpret_cast<const uint32_t *>(logic + LOGIC_FRAME_OFFSET);
        if (frame == lastFrame) {
            return;
        }
        lastFrame = frame;
        if (frame == 0 || frame % LOG_INTERVAL != 0 || failed) {
            return;
        }
        if (file == nullptr) {
            wchar_t path[MAX_PATH];
            const DWORD length = GetTempPathW(MAX_PATH, path);
            if (length == 0 || length + 32 > MAX_PATH || wcscat_s(path, L"bfme2-logic-crc.csv") != 0 || _wfopen_s(&file, path, L"w") != 0 ||
                file == nullptr) {
                failed = true;
                return;
            }
        }
        auto *lite = reinterpret_cast<uint8_t *>(LITE_CRC);
        *lite = 1;
        const uint32_t crc = reinterpret_cast<uint32_t (__thiscall *)(void *, uint32_t)>(GET_CRC)(logic, 0);
        *lite = 0;
        fprintf(file, "%u,%08X\n", frame, crc);
        fflush(file);
    }
}

bool instrumentation::applyLogicCrcLog() {
    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();

    const uintptr_t entry = cave.here();
    cave.emit8(0x60); // pushad
    cave.emit8(0x56); // push esi: GameLogic
    cave.call(reinterpret_cast<uintptr_t>(&logCrc));
    cave.emit8(0x61); // popad
    const BYTE gate[2] = {0x84, 0xDB}; // test bl, bl: the heartbeat's own gate
    cave.emit(gate);
    cave.jcc(JE, HEARTBEAT_SKIPPED);
    cave.jmp(HEARTBEAT_SENT);

    // test bl, bl / je 0x0062E876
    const BYTE stock[8] = {0x84, 0xDB, 0x0F, 0x84, 0x11, 0x01, 0x00, 0x00};
    set.hook(HEARTBEAT_GATE, stock, entry);
    // mov ecx, esi / mov byte [liteCRC], 1 / call GameLogic::getCRC: the producer, on esi as here, with the lite flag
    const BYTE producerOnEsi[2] = {0x8B, 0xCE};
    set.expect(0x0062E7E6, producerOnEsi);
    const BYTE liteSet[7] = {0xC6, 0x05, 0xC7, 0x87, 0xDE, 0x00, 0x01};
    set.expect(0x0062E7E8, liteSet);
    const BYTE producerCall[5] = {0xE8, 0x92, 0x70, 0xFF, 0xFF};
    set.expect(0x0062E7EF, producerCall);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
