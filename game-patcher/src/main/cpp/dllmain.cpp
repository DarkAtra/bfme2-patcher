#include <winsock2.h>
#include <easyhook.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "patches.h"

using namespace std;

// Counts the occurrences of the pattern in the executable pages of a loaded image and returns the first one.
static BYTE *findInImage(HMODULE module, const BYTE *pattern, const size_t patternLength, size_t &count) {

    auto *base = reinterpret_cast<BYTE *>(module);
    const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dosHeader->e_lfanew);
    const uintptr_t imageEnd = reinterpret_cast<uintptr_t>(base) + ntHeaders->OptionalHeader.SizeOfImage;
    constexpr DWORD EXECUTABLE = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    BYTE *first = nullptr;
    count = 0;
    MEMORY_BASIC_INFORMATION memoryInfo;
    uintptr_t address = reinterpret_cast<uintptr_t>(base);
    while (address < imageEnd && VirtualQuery(reinterpret_cast<void *>(address), &memoryInfo, sizeof(memoryInfo))) {
        const uintptr_t regionStart = reinterpret_cast<uintptr_t>(memoryInfo.BaseAddress);
        const uintptr_t regionEnd = regionStart + memoryInfo.RegionSize < imageEnd ? regionStart + memoryInfo.RegionSize : imageEnd;
        address = regionStart + memoryInfo.RegionSize;
        if (memoryInfo.State != MEM_COMMIT || (memoryInfo.Protect & EXECUTABLE) == 0 || (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            continue;
        }
        for (uintptr_t position = regionStart; position + patternLength <= regionEnd; position++) {
            if (memcmp(reinterpret_cast<const void *>(position), pattern, patternLength) == 0 && count++ == 0) {
                first = reinterpret_cast<BYTE *>(position);
            }
        }
    }

    return first;
}

// See: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-gethostbyname
// hostent *WSAAPI gethostbyname(
//   const char *name
// );
hostent *WSAAPI Hooked_gethostbyname(const char *name) {

    if (strcmp("gpcm.gamespy.com", name) == 0) {
        return gethostbyname("gpcm.server.cnc-online.net");
    } else if (strcmp("peerchat.gamespy.com", name) == 0) {
        return gethostbyname("peerchat.server.cnc-online.net");
    } else if (strcmp("psweb.gamespy.com", name) == 0) {
        return gethostbyname("server.cnc-online.net");
    } else if (strcmp("arenasdk.gamespy.com", name) == 0) {
        return gethostbyname("server.cnc-online.net");
    } else if (strcmp("lotrbfme.arenasdk.gamespy.com", name) == 0) {
        return gethostbyname("server.cnc-online.net");
    } else if (strcmp("ingamead.gamespy.com", name) == 0) {
        return gethostbyname("server.cnc-online.net");
    } else if (strcmp("master.gamespy.com", name) == 0) {
        return gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme.available.gamespy.com", name) == 0) {
        return gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme.master.gamespy.com", name) == 0) {
        return gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme.ms13.gamespy.com", name) == 0) {
        return gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.available.gamespy.com", name) == 0) {
        return gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.master.gamespy.com", name) == 0) {
        return gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.ms9.gamespy.com", name) == 0) {
        return gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("gamestats.gamespy.com", name) == 0) {
        return gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("lotrbme.gamestats.gamespy.com", name) == 0) {
        return gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.gamestats.gamespy.com", name) == 0) {
        return gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("lotrbme2wk.gamestats.gamespy.com", name) == 0) {
        return gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("servserv.generals.ea.com", name) == 0) {
        return gethostbyname("http.server.cnc-online.net");
    } else if (strcmp("na.llnet.eadownloads.ea.com", name) == 0) {
        return gethostbyname("http.server.cnc-online.net");
    } else if (strcmp("bfme.fesl.ea.com", name) == 0) {
        return gethostbyname("login.server.cnc-online.net");
    } else if (strcmp("bfme2.fesl.ea.com", name) == 0) {
        return gethostbyname("login.server.cnc-online.net");
    } else if (strcmp("bfme2-ep1-pc.fesl.ea.com", name) == 0) {
        return gethostbyname("login.server.cnc-online.net");
    }

    return gethostbyname(name);
}

HOOK_TRACE_INFO hHook = {nullptr}; // keep track of our hook

// The log only uses kernel32 and the C runtime: DllMain runs under the loader lock, where user32 (a message box) is not safe.
static HANDLE logFile = INVALID_HANDLE_VALUE;

// <game folder>\.patcher\game-patcher-log.txt, next to the updater's logs, written anew at every start
static void openLog() {

    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    wchar_t *folderEnd = wcsrchr(path, L'\\');
    if (folderEnd == nullptr) {
        return;
    }
    folderEnd[1] = L'\0';
    if (wcscat_s(path, L".patcher\\game-patcher-log.txt") != 0) {
        return;
    }
    logFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

static void writeLog(const char *level, const wchar_t *message) {

    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[512];
    const int length = _snprintf_s(line, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u %s: %ls\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, level, message);
    if (length <= 0) {
        return;
    }
    OutputDebugStringA(line);
    if (logFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(logFile, line, static_cast<DWORD>(length), &written, nullptr);
    }
}

static void closeLog() {
    if (logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(logFile);
        logFile = INVALID_HANDLE_VALUE;
    }
}

// this export is only required when the dll is loaded by manipulating the process's import table
// however, it does not cause any harm even when the dll is still loaded via CreateRemoteThread and LoadLibraryA
extern "C" __declspec(dllexport) void Noop() {}

BOOL APIENTRY DllMain(HINSTANCE /*hinstDLL*/, const DWORD fdwReason, LPVOID lpvReserved) {

    if (fdwReason == DLL_PROCESS_ATTACH) {

        openLog();
        writeLog("INFO", L"Loaded game-patcher.");

        const wchar_t *failedPatches[MAX_FAILED_PATCHES];
        const size_t failedPatchCount = applyPatches(failedPatches);
        for (size_t i = 0; i < failedPatchCount; i++) {
            wchar_t message[256];
            swprintf_s(message, L"Failed to apply the patch \"%ls\", the game runs without it.", failedPatches[i]);
            writeLog("WARNING", message);
        }
        if (failedPatchCount == 0) {
            writeLog("INFO", L"Applied every enabled patch.");
        }

        const NTSTATUS result = LhInstallHook(
            GetProcAddress(GetModuleHandle(TEXT("ws2_32")), "gethostbyname"),
            Hooked_gethostbyname,
            nullptr,
            &hHook
        );
        if (FAILED(result)) {
            wchar_t message[384];
            swprintf_s(message, L"Failed to hook gethostbyname (0x%08lX: %ls). Online gameplay might not be possible.", result, RtlGetLastErrorString());
            writeLog("SEVERE", message);
        } else {
            // A new hook intercepts no thread until it gets an ACL; an exclusive ACL without thread ids intercepts every thread.
            ULONG ACLEntries[1] = {};
            const NTSTATUS aclResult = LhSetExclusiveACL(ACLEntries, 0, &hHook);
            if (FAILED(aclResult)) {
                wchar_t message[384];
                swprintf_s(message, L"Failed to activate the gethostbyname hook (0x%08lX: %ls). Online gameplay might not be possible.", aclResult, RtlGetLastErrorString());
                writeLog("SEVERE", message);
            } else {
                writeLog("INFO", L"Hooked gethostbyname.");
            }
        }

        const HANDLE currentProcess = GetCurrentProcess();

        const BYTE search[] = {0x89, 0x55, 0x88, 0x83, 0x7D, 0x88, 0x08, 0x74, 0x08};
        constexpr BYTE patch[] = {0xEB, 0x46, 0x90, 0x90};

        size_t matches;
        BYTE *addressToModify = findInImage(GetModuleHandleA(nullptr), search, sizeof(search), matches);
        if (matches == 0) {
            writeLog("SEVERE", L"Failed to patch the certificate check: its code was not found. Online gameplay might not be possible.");
        } else if (matches > 1) {
            wchar_t message[256];
            swprintf_s(message, L"Failed to patch the certificate check: its code was found %zu times. Online gameplay might not be possible.", matches);
            writeLog("SEVERE", message);
        } else {
            SIZE_T bytesWritten;
            if (WriteProcessMemory(currentProcess, addressToModify + 3, patch, sizeof(patch), &bytesWritten)) {
                FlushInstructionCache(currentProcess, addressToModify + 3, sizeof(patch));
                writeLog("INFO", L"Patched the certificate check.");
            } else {
                writeLog("SEVERE", L"Failed to patch the certificate check: the write failed. Online gameplay might not be possible.");
            }
        }

        closeLog();

    } else if (fdwReason == DLL_PROCESS_DETACH && lpvReserved == nullptr) {
        // Only when the dll is unloaded from a running process. At process exit the other threads are already gone and waiting
        // under the loader lock could hang the exit, so nothing is cleaned up then.
        LhUninstallHook(&hHook);
        LhWaitForPendingRemovals();
    }

    return true;
}
