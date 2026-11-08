#include <windows.h>
#include <dbghelp.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "crash_dump.h"
#include "patch_set.h"

// Crash dumps
//
// Problem: the game's crash handler (0x0043D610, and 0x00438180 behind a try block) writes DUMP_<version>_<time>_<id>_<pid>_<tid>.dmp
// into the game folder with the game's dbghelp (6.3, 2003) as MiniDumpWithDataSegs: threads, stacks, modules and their data
// sections, but no heap memory and nothing about the patches. A crash that trips over memory corrupted earlier cannot be traced
// with that.
// Fix: the dump function (0x0043BE80) calls MiniDumpWriteDump through [ebp - 0x14] at 0x0043C022, with the file and the
// exception already prepared; that call goes to writeDump instead. It writes the same file with the system's dbghelp and adds
// the memory around the crashed thread's registers and the fault address, the memory its stack points to
// (MiniDumpWithIndirectlyReferencedMemory), the patches' caves, and a comment stream naming the patches applied and the cave
// of each. Memory that cannot be read while the dump is written (another thread freed it after the ranges were clipped to
// readable memory) is left out and the dump still counts as written: it holds everything the game's dump would and more. If
// the writer itself fails, the file is emptied and the game's own call is made.

using namespace patching;

namespace {

    constexpr uintptr_t GET_WRITER = 0x0043BE96;
    constexpr uintptr_t ARGUMENTS = 0x0043C001;
    constexpr uintptr_t WRITE_CALL = 0x0043C021;
    constexpr uintptr_t WRITE_RESUME = 0x0043C027;
    constexpr uintptr_t WRITER_NAME = 0x00BD4E74;

    // around each register, enough for the neighbouring heap blocks
    constexpr uintptr_t REGISTER_WINDOW = 0x4000;
    constexpr size_t MAX_RANGES = 192;
    constexpr size_t COMMENT_SIZE = 8192;

    struct Range {
        uintptr_t base;
        uintptr_t end;
    };

    // filled while the game is crashing, so nothing is allocated
    Range ranges[MAX_RANGES];
    size_t rangeCount;
    size_t nextRange;
    char comment[COMMENT_SIZE];
    size_t commentLength;

    // Adds the readable parts of [base, end).
    void addRange(const uintptr_t base, const uintptr_t end) {
        uintptr_t address = base;
        while (address < end && rangeCount < MAX_RANGES) {
            MEMORY_BASIC_INFORMATION info;
            if (!VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info))) {
                return;
            }
            const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (regionEnd <= address) {
                return;
            }
            const uintptr_t pieceEnd = regionEnd < end ? regionEnd : end;
            if (info.State == MEM_COMMIT && (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0) {
                ranges[rangeCount++] = {address, pieceEnd};
            }
            address = pieceEnd;
        }
    }

    void addAround(const uintptr_t value) {
        const uintptr_t base = value > REGISTER_WINDOW ? value - REGISTER_WINDOW : 0;
        const uintptr_t end = value < UINTPTR_MAX - REGISTER_WINDOW ? value + REGISTER_WINDOW : UINTPTR_MAX;
        addRange(base, end);
    }

    // sorted and without overlaps, so no byte is written twice
    void mergeRanges() {
        for (size_t i = 1; i < rangeCount; i++) {
            const Range range = ranges[i];
            size_t j = i;
            while (j > 0 && ranges[j - 1].base > range.base) {
                ranges[j] = ranges[j - 1];
                j--;
            }
            ranges[j] = range;
        }
        size_t merged = 0;
        for (size_t i = 0; i < rangeCount; i++) {
            if (merged > 0 && ranges[i].base <= ranges[merged - 1].end) {
                if (ranges[i].end > ranges[merged - 1].end) {
                    ranges[merged - 1].end = ranges[i].end;
                }
            } else {
                ranges[merged++] = ranges[i];
            }
        }
        rangeCount = merged;
    }

    void collectRanges(const MINIDUMP_EXCEPTION_INFORMATION *exception) {
        rangeCount = 0;
        nextRange = 0;
        // the caves first, so the register windows cannot use up the room they need
        const Records &patches = records();
        for (size_t i = 0; i < patches.caveCount; i++) {
            addRange(patches.caves[i].base, patches.caves[i].base + CAVE_SIZE);
        }
        if (exception != nullptr && exception->ExceptionPointers != nullptr) {
            const CONTEXT *context = exception->ExceptionPointers->ContextRecord;
            if (context != nullptr) {
                const DWORD registers[] = {
                    context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp, context->Eip
                };
                for (const DWORD value : registers) {
                    addAround(value);
                }
            }
            const EXCEPTION_RECORD *record = exception->ExceptionPointers->ExceptionRecord;
            if (record != nullptr && record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
                addAround(record->ExceptionInformation[1]);
            }
        }
        mergeRanges();
    }

    void append(const char *format, ...) {
        if (commentLength >= COMMENT_SIZE - 1) {
            return;
        }
        va_list arguments;
        va_start(arguments, format);
        const int written = _vsnprintf_s(comment + commentLength, COMMENT_SIZE - commentLength, _TRUNCATE, format, arguments);
        va_end(arguments);
        commentLength = written < 0 ? COMMENT_SIZE - 1 : commentLength + static_cast<size_t>(written);
    }

    void appendName(const wchar_t *name) {
        char narrow[128];
        if (name == nullptr || WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr) == 0) {
            append("?");
            return;
        }
        append("%s", narrow);
    }

    DWORD moduleTimestamp() {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&moduleTimestamp), &module)) {
            return 0;
        }
        const auto *base = reinterpret_cast<const uint8_t *>(module);
        const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        const auto *ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dosHeader->e_lfanew);
        return ntHeaders->FileHeader.TimeDateStamp;
    }

    void buildComment() {
        commentLength = 0;
        comment[0] = '\0';
        append("game-patcher (link time 0x%08lX)\r\n", moduleTimestamp());
        const Records &patches = records();
        for (size_t i = 0; i < patches.patchCount; i++) {
            append(patches.patches[i].applied ? "applied: " : "failed: ");
            appendName(patches.patches[i].name);
            append("\r\n");
        }
        for (size_t i = 0; i < patches.caveCount; i++) {
            append("cave 0x%08X-0x%08X: ", static_cast<unsigned>(patches.caves[i].base),
                   static_cast<unsigned>(patches.caves[i].base + CAVE_SIZE));
            appendName(patches.caves[i].owner);
            append("\r\n");
        }
    }

    BOOL CALLBACK dumpCallback(PVOID, const PMINIDUMP_CALLBACK_INPUT input, PMINIDUMP_CALLBACK_OUTPUT output) {
        if (input == nullptr || output == nullptr) {
            return FALSE;
        }
        switch (input->CallbackType) {
            case MemoryCallback:
                if (nextRange >= rangeCount) {
                    output->MemoryBase = 0;
                    output->MemorySize = 0;
                    return FALSE;
                }
                output->MemoryBase = ranges[nextRange].base;
                output->MemorySize = static_cast<ULONG>(ranges[nextRange].end - ranges[nextRange].base);
                nextRange++;
                return TRUE;
            case CancelCallback:
                output->Cancel = FALSE;
                output->CheckCancel = FALSE;
                return TRUE;
            case ReadMemoryFailureCallback:
                output->Status = S_OK;
                return TRUE;
            default:
                return TRUE;
        }
    }
}

MiniDumpWriteDumpFunction systemDumpWriter() {
    wchar_t path[MAX_PATH];
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length + 13 >= MAX_PATH) {
        return nullptr;
    }
    wcscat_s(path, L"\\dbghelp.dll");
    const HMODULE dbghelp = LoadLibraryW(path);
    if (dbghelp == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<MiniDumpWriteDumpFunction>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
}

namespace {

    bool writeExtendedDump(HANDLE process, const DWORD processId, HANDLE file, const MINIDUMP_TYPE gameType,
                           PMINIDUMP_EXCEPTION_INFORMATION exception) {
        const MiniDumpWriteDumpFunction writer = systemDumpWriter();
        if (writer == nullptr) {
            return false;
        }
        collectRanges(exception);
        buildComment();

        MINIDUMP_USER_STREAM stream;
        stream.Type = CommentStreamA;
        stream.BufferSize = static_cast<ULONG>(commentLength + 1);
        stream.Buffer = comment;
        MINIDUMP_USER_STREAM_INFORMATION streams;
        streams.UserStreamCount = 1;
        streams.UserStreamArray = &stream;
        MINIDUMP_CALLBACK_INFORMATION callback;
        callback.CallbackRoutine = dumpCallback;
        callback.CallbackParam = nullptr;

        const auto type = static_cast<MINIDUMP_TYPE>(gameType | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules
                                                     | MiniDumpWithProcessThreadData);
        return writer(process, processId, file, type, exception, &streams, &callback) != FALSE;
    }

    bool writeExtendedDumpGuarded(HANDLE process, const DWORD processId, HANDLE file, const MINIDUMP_TYPE gameType,
                                  PMINIDUMP_EXCEPTION_INFORMATION exception) {
        __try {
            return writeExtendedDump(process, processId, file, gameType, exception);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // stdcall like MiniDumpWriteDump, with the game's function in front: the cave pushes it last
    BOOL WINAPI writeDump(const MiniDumpWriteDumpFunction gameWriter, HANDLE process, const DWORD processId, HANDLE file,
                          const MINIDUMP_TYPE gameType, PMINIDUMP_EXCEPTION_INFORMATION exception,
                          PMINIDUMP_USER_STREAM_INFORMATION userStreams, PMINIDUMP_CALLBACK_INFORMATION callback) {
        if (writeExtendedDumpGuarded(process, processId, file, gameType, exception)) {
            return TRUE;
        }
        SetFilePointer(file, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
        return gameWriter(process, processId, file, gameType, exception, userStreams, callback);
    }
}

bool applyCrashDump() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // In place of push eax / call [ebp - 0x14] / cmp ebx, edi: hands the game's MiniDumpWriteDump to writeDump in front of the
    // seven arguments the game pushed.
    Cave cave(caveMemory);
    cave.emit8(0x50); // push eax: the process
    const BYTE pushGameWriter[3] = {0xFF, 0x75, 0xEC}; // push dword [ebp - 0x14]
    cave.emit(pushGameWriter);
    cave.call(reinterpret_cast<uintptr_t>(&writeDump));
    const BYTE compare[2] = {0x3B, 0xDF}; // cmp ebx, edi: whether a privilege is to be restored
    cave.emit(compare);
    cave.jmp(WRITE_RESUME);

    static PatchSet set;
    set.reset();

    // "MiniDumpWriteDump"
    const BYTE writerName[18] = {'M', 'i', 'n', 'i', 'D', 'u', 'm', 'p', 'W', 'r', 'i', 't', 'e', 'D', 'u', 'm', 'p', 0};
    set.expect(WRITER_NAME, writerName);
    // push the name / push the dbghelp module / call [GetProcAddress] / test eax, eax / mov [ebp - 0x14], eax
    const BYTE getWriter[17] = {0x68, 0x74, 0x4E, 0xBD, 0x00, 0x50, 0xFF, 0x15, 0x8C, 0x01, 0xBD, 0x00, 0x85, 0xC0, 0x89, 0x45, 0xEC};
    set.expect(GET_WRITER, getWriter);
    // the dump type (1 + full), then push 0 / push 0 / push the exception information / push the type / push the file /
    // push GetCurrentProcessId() / GetCurrentProcess()
    const BYTE arguments[32] = {
        0x8A, 0x55, 0x0C, 0x33, 0xC9, 0x84, 0xD2, 0x0F, 0x95, 0xC1, 0x57, 0x57, 0x8D, 0x45, 0xF0, 0x50, 0x41, 0x51, 0x56, 0xFF,
        0x15, 0x44, 0x01, 0xBD, 0x00, 0x50, 0xFF, 0x15, 0xC8, 0x01, 0xBD, 0x00
    };
    set.expectBytes(ARGUMENTS, arguments);
    // push eax / call [ebp - 0x14] / cmp ebx, edi
    const BYTE writeCall[6] = {0x50, 0xFF, 0x55, 0xEC, 0x3B, 0xDF};
    set.hook(WRITE_CALL, writeCall, reinterpret_cast<uintptr_t>(caveMemory));
    // je: no privilege to restore
    const BYTE resume[2] = {0x74, 0x12};
    set.expect(WRITE_RESUME, resume);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
