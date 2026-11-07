#include <windows.h>
#include <cstdint>

#include "game_memory.h"
#include "network_command_send.h"
#include "patch_set.h"

// Network command send
//
// Problem: the game hands the local player's commands to the network only in the network update, once per logic frame (200 ms),
// so a command waits 100 ms on average before it leaves the computer. On the host that costs nothing: the packet router gives
// every command it relays the frame after its current one and runs that frame in the same update, whenever the command was
// queued. A player who does not host loses the wait: a command that leaves at once reaches the router up to one router frame
// earlier, with a chance equal to the wait it skipped divided by 200 ms, i.e. 100 ms on average (measured wait: 99 ms median).
// Fix: on a peer that does not host, every client frame of a networked match hands the command list's network commands (types
// 1001 to 1998) to the connection manager as the network update does (sendLocalGameMessage, removeMessage, delete), and runs the
// network's lite update (the connection manager's packets out and in), which EA itself runs every client frame while such a peer
// waits for data. Deterministic: the sender never chooses a command's frame; the router assigns frame and time stamp when it
// relays it (relayCommand), and the command ids keep their order. The logic dispatches the command list in its first phase,
// after the network update, so it still never sees network commands. The host, the network update's other steps (type 0x1D,
// the player leave check) and games without a network are left alone.
//
// Sites: updateNetworkAndLogic (0x006329B0) runs every client frame; its profiler call (0x006329BA) is redirected. The command
// loop of Network::getCommandsFromCommandList (0x0065E45A): TheCommandList (0x00DE639C, first message + 0xC, next + 0x4, type
// + 0x10, removeMessage vtable + 0x40), the network's status (+ 0x10, 1 = in game) and connection manager (+ 0xC), the send call
// (0x0065E4B8, whose target the network stats may have redirected; called through it so they still see these sends), the delete
// (vtable + 0, then 0x0042F6A0). The lite update is the network's vtable (0x00C0C1F8) + 0x3C. The local slot (+ 0x12028) and the
// packet router's slot (+ 0x1202C) of the connection manager tell the host apart.

using namespace patching;

namespace {

    constexpr uintptr_t THE_NETWORK = 0x00DE4468;
    constexpr uintptr_t THE_COMMAND_LIST = 0x00DE639C;
    constexpr uintptr_t PROFILER_CALL = 0x006329BA;
    constexpr uintptr_t PROFILER_BEGIN = 0x00AD9AB0;
    constexpr uintptr_t SEND_CALL = 0x0065E4B8;
    constexpr uintptr_t FREE = 0x0042F6A0;

    constexpr int FIRST_NETWORK_TYPE = 1001;
    constexpr int LAST_NETWORK_TYPE = 1998;

    typedef void (__thiscall *Send)(uintptr_t connections, uintptr_t message);
    typedef void (__thiscall *Remove)(uintptr_t commandList, uintptr_t message);
    typedef void *(__thiscall *DeleteInstance)(uintptr_t message, int flags);
    typedef void (__cdecl *Free)(void *memory);
    typedef void (__thiscall *LiteUpdate)(uintptr_t network, int argument);

    void __cdecl sendCommands() {
        const auto network = read<uintptr_t>(THE_NETWORK);
        if (network == 0 || read<int>(network + 0x10) != 1) {
            return;
        }
        const auto connections = read<uintptr_t>(network + 0xC);
        const auto commands = read<uintptr_t>(THE_COMMAND_LIST);
        if (connections == 0 || commands == 0 || read<int>(connections + 0x12028) == read<int>(connections + 0x1202C)) {
            return;
        }
        const auto send = reinterpret_cast<Send>(SEND_CALL + 5 + read<int32_t>(SEND_CALL + 1));
        for (uintptr_t message = read<uintptr_t>(commands + 0xC); message != 0;) {
            const auto next = read<uintptr_t>(message + 4);
            const int type = read<int>(message + 0x10);
            if (type >= FIRST_NETWORK_TYPE && type <= LAST_NETWORK_TYPE) {
                send(connections, message);
                reinterpret_cast<Remove>(read<uintptr_t>(read<uintptr_t>(commands) + 0x40))(commands, message);
                reinterpret_cast<Free>(FREE)(reinterpret_cast<DeleteInstance>(read<uintptr_t>(read<uintptr_t>(message)))(message, 0));
            }
            message = next;
        }
        reinterpret_cast<LiteUpdate>(read<uintptr_t>(read<uintptr_t>(network) + 0x3C))(network, 0);
    }
}

bool applyNetworkCommandSend() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);

    // entered by the profiler call, its argument above the return address: send, then on to the profiler as if called from there
    const uintptr_t entry = cave.here();
    cave.emit8(0x60); // pushad
    cave.call(reinterpret_cast<uintptr_t>(&sendCommands));
    cave.emit8(0x61); // popad
    cave.jmp(PROFILER_BEGIN);

    static PatchSet set;
    set.reset();

    // push ebp / push esi / push edi / push the profiler name / mov ebp, ecx, then the profiler call and pop ecx behind it
    const BYTE head[10] = {0x55, 0x56, 0x57, 0x68, 0x50, 0xE3, 0xBF, 0x00, 0x8B, 0xE9};
    set.expect(0x006329B0, head);
    const BYTE profilerCall[5] = {0xE8, 0xF1, 0x70, 0x4A, 0x00};
    set.redirectCall(PROFILER_CALL, profilerCall, entry);
    const BYTE afterCall[8] = {0x8B, 0x7C, 0x24, 0x14, 0x83, 0xFF, 0x01, 0x59};
    set.expect(PROFILER_CALL + 5, afterCall);

    // the command loop: TheCommandList's first message, the type range, the next message, the status and the connection manager
    const BYTE firstMessage[11] = {0xA1, 0x9C, 0x63, 0xDE, 0x00, 0x53, 0x56, 0x57, 0x8B, 0x78, 0x0C};
    set.expect(0x0065E467, firstMessage);
    const BYTE loopHead[24] = {
        0x8B, 0x47, 0x10, 0x3D, 0xE8, 0x03, 0x00, 0x00, 0x8B, 0x5F, 0x04, 0x7E, 0x36, 0x3D, 0xCF, 0x07, 0x00, 0x00, 0x7D, 0x2F, 0x83,
        0x7E, 0x10, 0x01
    };
    set.expect(0x0065E47C, loopHead);
    const BYTE connectionsTest[5] = {0x8B, 0x4E, 0x0C, 0x85, 0xC9};
    set.expect(0x0065E496, connectionsTest);
    // mov ecx, [esi + 0xC] / push edi / call (its target read at run time)
    const BYTE sendCall[5] = {0x8B, 0x4E, 0x0C, 0x57, 0xE8};
    set.expect(SEND_CALL - 4, sendCall);
    const BYTE sendTarget[5] = {0xB8, 0x0C, 0x54, 0xBA, 0x00};
    set.expect(0x008D51E6, sendTarget);
    // removeMessage, deleteInstance(0), free
    const BYTE removeAndDelete[24] = {
        0x8B, 0x0D, 0x9C, 0x63, 0xDE, 0x00, 0x8B, 0x01, 0x57, 0xFF, 0x50, 0x40, 0x8B, 0x07, 0x8B, 0xCF, 0x6A, 0x00, 0xFF, 0x10, 0x50, 0xE8,
        0x8C, 0x11
    };
    set.expect(0x0065E4FA, removeAndDelete);
    const BYTE freeCleanUp[3] = {0xDD, 0xFF, 0x59};
    set.expect(0x0065E512, freeCleanUp);
    // the network's vtable: + 0x3C is the lite update, which runs the connection manager's update
    const BYTE liteUpdateSlot[4] = {0xAE, 0xD8, 0x65, 0x00};
    set.expect(0x00C0C234, liteUpdateSlot);
    const BYTE liteUpdate[8] = {0x8B, 0xC1, 0x83, 0x78, 0x0C, 0x00, 0x74, 0x14};
    set.expect(0x0065D8AE, liteUpdate);
    // TheNetwork, and the connection manager's local and packet router slots
    const BYTE loadNetwork[6] = {0x8B, 0x0D, 0x68, 0x44, 0xDE, 0x00};
    set.expect(0x006329D9, loadNetwork);
    const BYTE slots[12] = {0x8B, 0x81, 0x28, 0x20, 0x01, 0x00, 0x3B, 0x81, 0x2C, 0x20, 0x01, 0x00};
    set.expect(0x008D4061, slots);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
