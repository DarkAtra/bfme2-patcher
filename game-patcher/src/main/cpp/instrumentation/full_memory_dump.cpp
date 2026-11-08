#include <windows.h>
#include <dbghelp.h>
#include <cstdint>
#include <cwchar>

#include "crash_dump.h"
#include "instrumentation/full_memory_dump.h"

// Full memory dump (debug mode, with GAME_PATCHER_FULL_DUMP=1 in the game's environment)
//
// The game's crash dump (with crash_dump.cpp) holds the memory around the crash, which says what crashed but not always why.
// This handler runs before the game's: the first bad write any thread makes in the game's own code (the kind of access
// violation a buffer overrun produces; reads are left alone, the game probes memory that way) is saved as a dump with all memory
// to "bfme2-crash-<process id>.dmp" in the temp folder, then the exception goes on to the game's handler as before. A dump is as
// big as the game's memory, about a gigabyte.

namespace {

    constexpr uintptr_t GAME_TEXT_BEGIN = 0x00401000;
    constexpr uintptr_t GAME_TEXT_END = 0x00BD0000;

    // not const because the test (tools/instrumentation/tests/crash-dump) points them at its own code
    uintptr_t textBegin = GAME_TEXT_BEGIN;
    uintptr_t textEnd = GAME_TEXT_END;
    LONG written = 0;

    void writeFullDump(EXCEPTION_POINTERS *exception) {
        const MiniDumpWriteDumpFunction writeDump = systemDumpWriter();
        if (writeDump == nullptr) {
            return;
        }

        wchar_t path[MAX_PATH];
        const DWORD length = GetTempPathW(MAX_PATH, path);
        if (length == 0 || length >= MAX_PATH) {
            return;
        }
        swprintf(path + length, MAX_PATH - length, L"bfme2-crash-%lu.dmp", GetCurrentProcessId());
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        MINIDUMP_EXCEPTION_INFORMATION information;
        information.ThreadId = GetCurrentThreadId();
        information.ExceptionPointers = exception;
        information.ClientPointers = FALSE;
        writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData),
                  &information, nullptr, nullptr);
        CloseHandle(file);
    }

    LONG CALLBACK handler(EXCEPTION_POINTERS *exception) {
        const EXCEPTION_RECORD &record = *exception->ExceptionRecord;
        const auto address = reinterpret_cast<uintptr_t>(record.ExceptionAddress);
        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2 && record.ExceptionInformation[0] == 1
            && address >= textBegin && address < textEnd && InterlockedExchange(&written, 1) == 0) {
            writeFullDump(exception);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

bool instrumentation::fullMemoryDumpRequested() {
    wchar_t value[4];
    const DWORD length = GetEnvironmentVariableW(L"GAME_PATCHER_FULL_DUMP", value, 4);
    return length == 1 && value[0] == L'1';
}

bool instrumentation::applyFullMemoryDump() {
    return AddVectoredExceptionHandler(1, handler) != nullptr;
}
