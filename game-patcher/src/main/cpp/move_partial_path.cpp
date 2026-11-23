#include <windows.h>
#include <cstdint>

#include "move_partial_path.h"
#include "patch_set.h"

// Partial paths continue
//
// Problem: a unit whose path search could not reach the goal gets a path to the closest cell it reached, walks there and then
// stops: its move order counts as done, it goes idle (and fights whatever is in range), however far the goal still is.
// Fix: at the end of such a path a unit on a move order plans again towards the same goal, a few times per order.
//
// A path to the closest reached cell sets the AI's retry flag (AI + 0x3C0). The move state (AIInternalMoveToState) was meant to try
// one more path then: its start (0x0074DADA) sets a "try one more repath" byte (state + 0x4A) and its update (0x00748E46) clears it
// when a path arrives that is not a partial one, but nothing ever acts on it, and the update's periodic re-plan only fires when the
// goal moves. So at the end of the partial path (0x007492E8, where both ways into the arrival meet) the state reports success.
//
// The byte becomes a budget: the state's start sets it to EXTRA_PATHS instead of 1, the update still clears it for a full path. At
// the arrival, a unit in the plain move state (state 1, the one player and script move orders use), whose path was partial, with
// budget left, whose path took it at least one cell and ends more than 40 units from the goal, asks for a new path towards the same
// goal through the state's own computePath (vtable + 0x44, as the update's re-plan does) and the state goes on (STATE_CONTINUE,
// 0x00749338). The budget is spent when the request is queued (AI + 0x3B1, waiting for a path); a request the game delays because the
// last path is less than two frames old is simply repeated on the next update. Everything else ends as before. The test reads only
// the game's state, and its float comparisons are exact in the dll's SSE code, so it decides the same on every machine.

using namespace patching;

namespace {

    constexpr uintptr_t STATE_START_BUDGET = 0x0074DBC9;
    constexpr uintptr_t ARRIVAL = 0x007492E8;
    constexpr uintptr_t ARRIVAL_RESUME = 0x007492EF;
    constexpr uintptr_t STATE_CONTINUE = 0x00749338;
    // thiscall(move state): whether the state adjusts its destination, the call the arrival starts with
    constexpr uintptr_t ADJUSTS_DESTINATION = 0x00745B3F;
    // thiscall(ai update): the id of the AI's current state; keeps ecx
    constexpr uintptr_t CURRENT_STATE = 0x00660AC1;

    constexpr BYTE EXTRA_PATHS = 4;
    constexpr uint32_t MOVE_TO_STATE = 1;
    constexpr float MIN_STEP = 10.0f;
    constexpr float GOAL_REACHED = 40.0f;

    constexpr size_t STATE_GOAL = 0x20;
    constexpr size_t STATE_BUDGET = 0x4A;
    constexpr size_t STATE_COMPUTE_PATH_SLOT = 0x44;
    constexpr size_t AI_PATH = 0x140;
    constexpr size_t AI_WAITING_FOR_PATH = 0x3B1;
    constexpr size_t AI_RETRY_PATH = 0x3C0;
    constexpr size_t PATH_FIRST_NODE = 0x04;
    constexpr size_t PATH_LAST_NODE = 0x08;
    constexpr size_t NODE_POSITION = 0x0C;

    template<typename T>
    T read(const void *base, const size_t offset) {
        return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
    }

    float squaredDistance2d(const float *a, const float *b) {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        return dx * dx + dy * dy;
    }

    // Whether the unit plans again instead of ending its move order; asks for the path when it does.
    bool __stdcall planAgain(uint8_t *state, uint8_t *ai) {
        if (reinterpret_cast<uint32_t (__thiscall *)(uint8_t *)>(CURRENT_STATE)(ai) != MOVE_TO_STATE) {
            return false;
        }
        if (read<uint8_t>(ai, AI_RETRY_PATH) == 0 || state[STATE_BUDGET] == 0) {
            return false;
        }
        const auto *path = read<const uint8_t *>(ai, AI_PATH);
        if (path == nullptr) {
            return false;
        }
        const auto *first = read<const uint8_t *>(path, PATH_FIRST_NODE);
        const auto *last = read<const uint8_t *>(path, PATH_LAST_NODE);
        if (first == nullptr || last == nullptr) {
            return false;
        }
        const auto *from = reinterpret_cast<const float *>(first + NODE_POSITION);
        const auto *to = reinterpret_cast<const float *>(last + NODE_POSITION);
        const auto *goal = reinterpret_cast<const float *>(state + STATE_GOAL);
        if (squaredDistance2d(from, to) < MIN_STEP * MIN_STEP || squaredDistance2d(to, goal) <= GOAL_REACHED * GOAL_REACHED) {
            return false;
        }
        const auto computePath = reinterpret_cast<uint8_t (__thiscall *)(uint8_t *)>(read<const uintptr_t *>(state, 0)[STATE_COMPUTE_PATH_SLOT / 4]);
        if (computePath(state) == 0) {
            return false;
        }
        if (read<uint8_t>(ai, AI_WAITING_FOR_PATH) != 0) {
            state[STATE_BUDGET]--;
        }
        return true;
    }
}

bool applyMovePartialPath() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    // pushad / push esi (ai) / push edi (state) / call planAgain / test al, al / popad (keeps the flags) / jne STATE_CONTINUE, else
    // the replaced mov ecx, edi / call ADJUSTS_DESTINATION and back
    const uintptr_t arrival = cave.here();
    cave.emit8(0x60);
    cave.emit8(0x56);
    cave.emit8(0x57);
    cave.call(reinterpret_cast<uintptr_t>(&planAgain));
    const BYTE testResult[2] = {0x84, 0xC0};
    cave.emit(testResult);
    cave.emit8(0x61);
    cave.jcc(JNE, STATE_CONTINUE);
    const BYTE loadState[2] = {0x8B, 0xCF};
    cave.emit(loadState);
    cave.call(ADJUSTS_DESTINATION);
    cave.jmp(ARRIVAL_RESUME);

    static PatchSet set;
    set.reset();

    // mov byte [esi + 0x4A], 1: the state's start sets the "try one more repath" byte
    const BYTE startByte[4] = {0xC6, 0x46, 0x4A, 0x01};
    const BYTE startBudget[4] = {0xC6, 0x46, 0x4A, EXTRA_PATHS};
    set.replace(STATE_START_BUDGET, startByte, startBudget);
    // cmp byte [eax + 0x3C0], 0 / mov edi, [ebp - 0xC] / mov esi, eax / jne / mov eax, edi / mov byte [eax + 0x4A], 0: the update clears
    // the byte when a path arrives without the AI's retry flag
    const BYTE budgetClear[20] = {0x80, 0xB8, 0xC0, 0x03, 0x00, 0x00, 0x00, 0x8B, 0x7D, 0xF4, 0x8B, 0xF0, 0x75, 0x06, 0x8B, 0xC7,
                                  0xC6, 0x40, 0x4A, 0x00};
    set.expect(0x00748F21, budgetClear);
    // cmp byte [esi + 0x3B1], 0: the update's test of the AI's waiting flag
    const BYTE waitingTest[7] = {0x80, 0xBE, 0xB1, 0x03, 0x00, 0x00, 0x00};
    set.expect(0x00748ED5, waitingTest);
    // mov eax, [edi] / mov ecx, edi / call [eax + 0x44]: the update's own re-plan through the state's computePath
    const BYTE computePathCall[7] = {0x8B, 0x07, 0x8B, 0xCF, 0xFF, 0x50, 0x44};
    set.expect(0x007491DC, computePathCall);

    // mov ecx, edi / call ADJUSTS_DESTINATION: the arrival
    const BYTE arrivalCode[7] = {0x8B, 0xCF, 0xE8, 0x50, 0xC8, 0xFF, 0xFF};
    set.hook(ARRIVAL, arrivalCode, arrival);
    // the 40-unit test against the path's last node before it: mov eax, [esi + 0x140] / ... / mov eax, [eax + 8] / ... / lea esi,
    // [eax + 0xC]
    const BYTE lastNode[3] = {0x8B, 0x40, 0x08};
    set.expect(0x00749287, lastNode);
    const BYTE lastNodePosition[3] = {0x8D, 0x70, 0x0C};
    set.expect(0x0074929D, lastNodePosition);
    // movss xmm0, [edi + 0x20]: the state's goal
    const BYTE stateGoal[5] = {0xF3, 0x0F, 0x10, 0x47, 0x20};
    set.expect(0x00749282, stateGoal);
    // test al, al / je: what follows the replaced call
    const BYTE arrivalNext[4] = {0x84, 0xC0, 0x74, 0x0A};
    set.expect(ARRIVAL_RESUME, arrivalNext);
    // xor eax, eax / pop edi / pop esi / pop ebx / leave / ret: STATE_CONTINUE
    const BYTE continueCode[7] = {0x33, 0xC0, 0x5F, 0x5E, 0x5B, 0xC9, 0xC3};
    set.expect(STATE_CONTINUE, continueCode);
    // mov eax, [ecx + 0x30] / mov eax, [eax + 4] / test eax, eax / je / mov eax, [eax + 4] / ret: the current state's id
    const BYTE currentState[14] = {0x8B, 0x41, 0x30, 0x8B, 0x40, 0x04, 0x85, 0xC0, 0x74, 0x04, 0x8B, 0x40, 0x04, 0xC3};
    set.expect(CURRENT_STATE, currentState);
    // mov eax, [ecx + 4] / test eax, eax / je: the path destructor walks the nodes from the first one at +4
    const BYTE pathDestructor[7] = {0x8B, 0x41, 0x04, 0x85, 0xC0, 0x74, 0x11};
    set.expect(0x007666FF, pathDestructor);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
