#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "network_stats.h"
#include "patch_set.h"

// Network stats
//
// Problem: where the command delay of a networked match comes from (the wait for the network update, the trip to and from the
// packet router, the frames an off-host peer keeps buffered, the frame limiter's network slowdown) cannot be told from playing.
// Fix: records, in every networked match, when each of the local player's commands was created, sent and executed, and on every
// attempt to start a logic frame whether the network allowed it, how many frames this peer has buffered and the slowdown, into
// <game folder>\.patcher\network-<local date>-<time>.csv. Every player of a match writes their own file. A sent command carries
// the player index the game stamped into it (+ 0x14), the one its execution shows; the player list is not read at the match
// start, where it still names player 0.
//
// The hooks only read the game's memory and keep every register, so the game runs exactly as without them. Commands are the
// messages of the types the network sends (1001 to 1998, 0x0065E45A): created by GameMessageList::appendMessage /
// insertMessage (0x00710E88 / 0x00710EB8, timestamps kept by message address), handed to the connection manager in the network
// update (0x0065E4B8, once per logic frame), executed by the logic's dispatcher (0x0062E8F1, every player's). The frame start
// attempt is the network's frame advance query in updateNetworkAndLogic's first phase (0x006329EB): above 0 the frame runs.
// The connection manager (TheNetwork + 0xC) holds the local slot (+ 0x12028), the packet router's slot (+ 0x1202C), the last
// frame it has every player's commands for (+ 0x1205C) and, on the packet router, the frame each slot has reached (+ 0x12060).
// Only slot numbers are written, no names or addresses.

using namespace patching;

namespace {

    constexpr uintptr_t THE_NETWORK = 0x00DE4468;
    constexpr uintptr_t THE_GAME_LOGIC = 0x00DE412C;
    constexpr uintptr_t THE_GAME_ENGINE = 0x00DE4324;
    constexpr uintptr_t SLOWDOWN_SCALAR = 0x00D9F498;
    constexpr uintptr_t LOGIC_RATE = 0x00D9F608;
    constexpr uintptr_t CLIENT_RATE = 0x00D9F60C;
    constexpr uintptr_t WRAP_IMMEDIATE = 0x0063264C;
    constexpr uintptr_t CATCH_UP = 0x00632A9B;

    constexpr uintptr_t ADVANCE_QUERY = 0x006329EB;
    constexpr uintptr_t ADVANCE_TEST = 0x006329F0;
    constexpr uintptr_t SEND_CALL = 0x0065E4B8;
    constexpr uintptr_t SEND_LOCAL_GAME_MESSAGE = 0x008D51E6;
    constexpr uintptr_t DISPATCH_CALL = 0x0062E8F1;
    constexpr uintptr_t DISPATCH = 0x00779A3D;
    constexpr uintptr_t APPEND_CONSTRUCT_CALL = 0x00710E9E;
    constexpr uintptr_t INSERT_CONSTRUCT_CALL = 0x00710ECE;
    constexpr uintptr_t GAME_MESSAGE_CONSTRUCTOR = 0x00710C2E;
    constexpr uintptr_t FRAMES_BEHIND = 0x008D4061;

    constexpr int FIRST_NETWORK_TYPE = 1001;
    constexpr int LAST_NETWORK_TYPE = 1998;
    constexpr int SLOTS = 8;
    constexpr size_t CREATED_ENTRIES = 64;
    constexpr int64_t FLUSH_INTERVAL_US = 2000000;

    template<typename T>
    T read(const uintptr_t address) {
        return *reinterpret_cast<const T *>(address);
    }

    struct Created {
        uintptr_t message;
        int64_t ticks;
        int frame;
    };

    // Only the game's main thread runs the hooked code.
    struct Recorder {
        HANDLE file = INVALID_HANDLE_VALUE;
        uintptr_t network = 0;
        int lastFrame = 0;
        LARGE_INTEGER frequency{};
        int64_t startTicks = 0;
        int64_t lastFlushUs = 0;
        size_t used = 0;
        char buffer[1 << 16];
        Created created[CREATED_ENTRIES];
        size_t createdNext = 0;
        // a refused frame start repeats every client frame until the frame may run; they go into the next start's line
        int refused = 0;
        int64_t firstRefusedUs = 0;
    };

    Recorder recorder;

    int64_t ticks() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart;
    }

    int64_t microseconds(const int64_t at) {
        return (at - recorder.startTicks) * 1000000 / recorder.frequency.QuadPart;
    }

    int logicFrame() {
        const auto logic = read<uintptr_t>(THE_GAME_LOGIC);
        return logic != 0 ? read<int>(logic + 0x40) : -1;
    }

    void flush() {
        if (recorder.file != INVALID_HANDLE_VALUE && recorder.used > 0) {
            DWORD written;
            WriteFile(recorder.file, recorder.buffer, static_cast<DWORD>(recorder.used), &written, nullptr);
        }
        recorder.used = 0;
    }

    void append(const char *format, ...) {
        if (recorder.file == INVALID_HANDLE_VALUE) {
            return;
        }
        for (int attempt = 0; attempt < 2; attempt++) {
            va_list arguments;
            va_start(arguments, format);
            const int length = _vsnprintf_s(recorder.buffer + recorder.used, sizeof(recorder.buffer) - recorder.used, _TRUNCATE,
                format, arguments);
            va_end(arguments);
            if (length >= 0) {
                recorder.used += length;
                return;
            }
            flush();
        }
    }

    void close() {
        flush();
        if (recorder.file != INVALID_HANDLE_VALUE) {
            CloseHandle(recorder.file);
            recorder.file = INVALID_HANDLE_VALUE;
        }
        recorder.network = 0;
    }

    void open(const uintptr_t network) {
        recorder.network = network;
        recorder.createdNext = 0;
        recorder.refused = 0;
        memset(recorder.created, 0, sizeof(recorder.created));

        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        wchar_t *folderEnd = length > 0 && length < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
        if (folderEnd == nullptr) {
            return;
        }
        SYSTEMTIME now;
        GetLocalTime(&now);
        const size_t folderLength = folderEnd + 1 - path;
        if (swprintf_s(folderEnd + 1, MAX_PATH - folderLength, L".patcher\\network-%04u%02u%02u-%02u%02u%02u.csv",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond) < 0) {
            return;
        }
        recorder.file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (recorder.file == INVALID_HANDLE_VALUE) {
            return;
        }

        QueryPerformanceFrequency(&recorder.frequency);
        recorder.startTicks = ticks();
        recorder.lastFlushUs = 0;
        FILETIME systemTime;
        GetSystemTimeAsFileTime(&systemTime);

        const auto connections = read<uintptr_t>(network + 0xC);
        append("# game-patcher network stats 2\r\n");
        append("# started %04u-%02u-%02u %02u:%02u:%02u.%03u local, UTC FILETIME %llu at us 0\r\n", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            static_cast<unsigned long long>(systemTime.dwHighDateTime) << 32 | systemTime.dwLowDateTime);
        append("# local slot %d, packet router slot %d\r\n",
            connections != 0 ? read<int>(connections + 0x12028) : -1, connections != 0 ? read<int>(connections + 0x1202C) : -1);
        append("# client rate %d, logic rate %d, wrap %d, %s\r\n", read<int>(CLIENT_RATE), read<int>(LOGIC_RATE),
            read<BYTE>(WRAP_IMMEDIATE), read<BYTE>(CATCH_UP) == 0xB8 ? "2.02 delay fix build" : "build without the 2.02 delay fix");
        append("# A,us,frame,advance,refused attempts before,first refused us,subframe,ratio,buffered,slowdown,ceilinghits"
            "[,slot 0..7 frame on the packet router]: a frame start the network allowed\r\n");
        append("# S,us,frame,type,created us,created frame,player index: a local command handed to the network\r\n");
        append("# X,us,frame,type,player index: a command executed\r\n");
    }

    // The network object of the running match or 0. Starts a file for a new match and finishes the last one when it ends.
    uintptr_t current() {
        const auto network = read<uintptr_t>(THE_NETWORK);
        const int frame = logicFrame();
        if (network != recorder.network || (network != 0 && frame < recorder.lastFrame)) {
            if (recorder.network != 0) {
                close();
            }
            if (network != 0) {
                open(network);
            }
        }
        recorder.lastFrame = frame;
        if (network != 0 && recorder.file != INVALID_HANDLE_VALUE) {
            const int64_t now = microseconds(ticks());
            if (now - recorder.lastFlushUs >= FLUSH_INTERVAL_US) {
                flush();
                recorder.lastFlushUs = now;
            }
        }
        return network;
    }

    bool isNetworkType(const int type) {
        return type >= FIRST_NETWORK_TYPE && type <= LAST_NETWORK_TYPE;
    }

    void __cdecl onMessageCreated(const uintptr_t message) {
        if (current() == 0 || !isNetworkType(read<int>(message + 0x10))) {
            return;
        }
        recorder.created[recorder.createdNext++ % CREATED_ENTRIES] = {message, ticks(), logicFrame()};
    }

    void __cdecl onSend(const uintptr_t message) {
        if (current() == 0) {
            return;
        }
        const int64_t now = microseconds(ticks());
        const int type = read<int>(message + 0x10);
        const int player = read<int>(message + 0x14);
        // newest first: the memory of a deleted message can come back for a later one
        for (size_t i = 1; i <= CREATED_ENTRIES && i <= recorder.createdNext; i++) {
            const Created &entry = recorder.created[(recorder.createdNext - i) % CREATED_ENTRIES];
            if (entry.message == message) {
                append("S,%lld,%d,%d,%lld,%d,%d\r\n", now, logicFrame(), type, microseconds(entry.ticks), entry.frame, player);
                return;
            }
        }
        append("S,%lld,%d,%d,,,%d\r\n", now, logicFrame(), type, player);
    }

    void __cdecl onDispatch(const uintptr_t message) {
        if (current() == 0) {
            return;
        }
        const int type = read<int>(message + 0x10);
        if (isNetworkType(type)) {
            append("X,%lld,%d,%d,%d\r\n", microseconds(ticks()), logicFrame(), type, read<int>(message + 0x14));
        }
    }

    void __cdecl onAdvanceQuery(const int advance) {
        const uintptr_t network = current();
        if (network == 0) {
            return;
        }
        const int64_t now = microseconds(ticks());
        if (advance <= 0) {
            if (recorder.refused++ == 0) {
                recorder.firstRefusedUs = now;
            }
            return;
        }
        const int frame = logicFrame();
        const auto engine = read<uintptr_t>(THE_GAME_ENGINE);
        const auto connections = read<uintptr_t>(network + 0xC);
        if (recorder.refused > 0) {
            append("A,%lld,%d,%d,%d,%lld", now, frame, advance, recorder.refused, recorder.firstRefusedUs);
        } else {
            append("A,%lld,%d,%d,0,", now, frame, advance);
        }
        recorder.refused = 0;
        append(",%d,%d,%d,%.3f,%d", engine != 0 ? read<int>(engine + 0x34) : 0, engine != 0 ? read<int>(engine + 0x38) : 0,
            connections != 0 ? read<int>(connections + 0x1205C) - frame : 0, read<float>(SLOWDOWN_SCALAR), read<int>(network + 0x34));
        if (connections != 0 && read<int>(connections + 0x12028) == read<int>(connections + 0x1202C)) {
            for (int slot = 0; slot < SLOTS; slot++) {
                if (read<uintptr_t>(connections + 4 + slot * 4) != 0) {
                    append(",%d", read<int>(connections + 0x12060 + slot * 4));
                } else {
                    append(",");
                }
            }
        }
        append("\r\n");
    }

    template<size_t N>
    void emitHandlerCall(Cave &cave, const BYTE (&pushArgument)[N], const uintptr_t handler) {
        cave.emit8(0x60);
        cave.emit(pushArgument);
        cave.call(handler);
        const BYTE pop[4] = {0x83, 0xC4, 0x04, 0x61};
        cave.emit(pop);
    }
}

bool applyNetworkStats() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);
    const BYTE pushEax[1] = {0x50};
    // the argument of a call the cave was entered by, behind pushad's 32 bytes and the return address
    const BYTE pushCallArgument[4] = {0xFF, 0x74, 0x24, 0x24};

    // in place of the advance query: run it, then record its answer in eax
    const uintptr_t advance = cave.here();
    const BYTE advanceQuery[5] = {0x8B, 0x01, 0xFF, 0x50, 0x54}; // mov eax, [ecx] / call [eax + 0x54]
    cave.emit(advanceQuery);
    emitHandlerCall(cave, pushEax, reinterpret_cast<uintptr_t>(&onAdvanceQuery));
    cave.jmp(ADVANCE_TEST);

    const uintptr_t send = cave.here();
    emitHandlerCall(cave, pushCallArgument, reinterpret_cast<uintptr_t>(&onSend));
    cave.jmp(SEND_LOCAL_GAME_MESSAGE);

    const uintptr_t dispatch = cave.here();
    emitHandlerCall(cave, pushCallArgument, reinterpret_cast<uintptr_t>(&onDispatch));
    cave.jmp(DISPATCH);

    // in place of the constructor call: construct with the caller's type argument (the constructor pops its copy), record the new
    // message in eax and return to the caller, popping its argument
    const uintptr_t construct = cave.here();
    const BYTE pushType[4] = {0xFF, 0x74, 0x24, 0x04}; // push dword [esp + 4]
    cave.emit(pushType);
    cave.call(GAME_MESSAGE_CONSTRUCTOR);
    emitHandlerCall(cave, pushEax, reinterpret_cast<uintptr_t>(&onMessageCreated));
    const BYTE returnPop[3] = {0xC2, 0x04, 0x00}; // ret 4
    cave.emit(returnPop);

    static PatchSet set;
    set.reset();

    // test ecx, ecx / je: TheNetwork; then the query, and test eax, eax / jg: the frame runs
    const BYTE networkTest[4] = {0x85, 0xC9, 0x74, 0x72};
    set.expect(ADVANCE_QUERY - 4, networkTest);
    set.hook(ADVANCE_QUERY, advanceQuery, advance);
    const BYTE advanceTest[4] = {0x85, 0xC0, 0x7F, 0x12};
    set.expect(ADVANCE_TEST, advanceTest);

    // mov ecx, [esi + 0xC] / push edi: the connection manager and the message
    const BYTE sendArguments[4] = {0x8B, 0x4E, 0x0C, 0x57};
    set.expect(SEND_CALL - 4, sendArguments);
    const BYTE sendCall[5] = {0xE8, 0x29, 0x6D, 0x27, 0x00};
    set.redirectCall(SEND_CALL, sendCall, send);

    // push 0 / push ebx / mov ecx, esi: the dispatcher's arguments, the message first
    const BYTE dispatchArguments[5] = {0x6A, 0x00, 0x53, 0x8B, 0xCE};
    set.expect(DISPATCH_CALL - 5, dispatchArguments);
    const BYTE dispatchCall[5] = {0xE8, 0x47, 0xB1, 0x14, 0x00};
    set.redirectCall(DISPATCH_CALL, dispatchCall, dispatch);

    // push [esp + 0xC] / mov ecx, eax: the type and the new memory; mov esi, eax: the message after the call
    const BYTE constructArguments[6] = {0xFF, 0x74, 0x24, 0x0C, 0x8B, 0xC8};
    const BYTE constructResult[2] = {0x8B, 0xF0};
    set.expect(APPEND_CONSTRUCT_CALL - 6, constructArguments);
    const BYTE appendConstruct[5] = {0xE8, 0x8B, 0xFD, 0xFF, 0xFF};
    set.redirectCall(APPEND_CONSTRUCT_CALL, appendConstruct, construct);
    set.expect(APPEND_CONSTRUCT_CALL + 5, constructResult);
    set.expect(INSERT_CONSTRUCT_CALL - 6, constructArguments);
    const BYTE insertConstruct[5] = {0xE8, 0x5B, 0xFD, 0xFF, 0xFF};
    set.redirectCall(INSERT_CONSTRUCT_CALL, insertConstruct, construct);
    set.expect(INSERT_CONSTRUCT_CALL + 5, constructResult);

    // the constructor: the local player's index from ThePlayerList + 0x10 + 0x54 into + 0x14, the type into + 0x10, ret 4
    const BYTE constructor[23] = {
        0x8B, 0xC1, 0xC7, 0x00, 0xFC, 0xFC, 0xC1, 0x00, 0x8B, 0x0D, 0x28, 0x49, 0xDE, 0x00, 0x8B, 0x49, 0x10, 0x8B, 0x49, 0x54, 0x89, 0x48,
        0x14
    };
    set.expect(GAME_MESSAGE_CONSTRUCTOR, constructor);
    const BYTE constructorType[7] = {0x8B, 0x4C, 0x24, 0x04, 0x89, 0x48, 0x10};
    set.expect(GAME_MESSAGE_CONSTRUCTOR + 0x17, constructorType);
    set.expect(GAME_MESSAGE_CONSTRUCTOR + 0x32, returnPop);

    // the connection manager's frames behind the packet router: local slot, router slot, last complete frame, connections from
    // + 4, the slots' frames
    const BYTE framesBehind[24] = {
        0x8B, 0x81, 0x28, 0x20, 0x01, 0x00, 0x3B, 0x81, 0x2C, 0x20, 0x01, 0x00, 0x74, 0x10, 0x8B, 0x81, 0x5C, 0x20, 0x01, 0x00, 0x8B, 0x0D,
        0x2C, 0x41
    };
    set.expect(FRAMES_BEHIND, framesBehind);
    const BYTE slotFrames[14] = {0x8D, 0x81, 0x60, 0x20, 0x01, 0x00, 0x5E, 0x83, 0xB8, 0xA4, 0xDF, 0xFE, 0xFF, 0x00};
    set.expect(FRAMES_BEHIND + 0x26, slotFrames);
    // network vtable + 0xF0, the run-ahead ceiling hits: mov eax, [ecx + 0x34] / ret
    const BYTE ceilingHitsSlot[4] = {0xB3, 0x41, 0x93, 0x00};
    set.expect(0x00C0C2E8, ceilingHitsSlot);
    const BYTE ceilingHits[4] = {0x8B, 0x41, 0x34, 0xC3};
    set.expect(0x009341B3, ceilingHits);
    // the globals read: TheNetwork, TheGameEngine, the client and the logic rate
    const BYTE loadNetwork[6] = {0x8B, 0x0D, 0x68, 0x44, 0xDE, 0x00};
    set.expect(0x006329D9, loadNetwork);
    const BYTE loadEngine[6] = {0x8B, 0x0D, 0x24, 0x43, 0xDE, 0x00};
    set.expect(0x0083BA0C, loadEngine);
    const BYTE loadClientRate[6] = {0xA1, 0x0C, 0xF6, 0xD9, 0x00, 0x99};
    set.expect(0x0063252F, loadClientRate);
    const BYTE loadLogicRate[5] = {0xA1, 0x08, 0xF6, 0xD9, 0x00};
    set.expect(0x0060A198, loadLogicRate);
    // the engine's sub-frame counter (+ 0x34) and ratio (+ 0x38), the logic frame (TheGameLogic + 0x40), the slowdown scalar
    const BYTE subFrame[3] = {0x8B, 0x4E, 0x34};
    set.expect(0x00632601, subFrame);
    const BYTE ratio[3] = {0x89, 0x46, 0x38};
    set.expect(0x0063261F, ratio);
    const BYTE frame[9] = {0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x8B, 0x49, 0x40};
    set.expect(0x00632628, frame);
    const BYTE slowdown[8] = {0xF3, 0x0F, 0x10, 0x05, 0x98, 0xF4, 0xD9, 0x00};
    set.expect(0x0063A043, slowdown);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
