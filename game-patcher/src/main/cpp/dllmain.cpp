#include <winsock2.h>
#include <easyhook.h>
#include <cstdio>
#include <cwchar>

#include "patches.h"

using namespace std;

// `patternEnd` points at the last byte of the pattern, not behind it.
__forceinline static BYTE *findPattern(BYTE *srcStart, BYTE *srcEnd, const BYTE *patternStart, const BYTE *patternEnd) {

    const BYTE *end = srcEnd - (patternEnd - patternStart + 1);

    for (BYTE *pos = srcStart; pos <= end; pos++) {
        const BYTE *s1 = pos - 1;
        const BYTE *p1 = patternStart - 1;

        while (*++s1 == *++p1) {
            if (p1 == patternEnd) {
                return pos;
            }
        }
    }

    return srcEnd;
}

__forceinline static BYTE *findPatternInProcessMemory(const BYTE *search, const BYTE *search_end) {

    // start searching from the beginning of the process's memory
    ULONG_PTR baseAddress = reinterpret_cast<ULONG_PTR>(GetModuleHandleA(nullptr));

    MEMORY_BASIC_INFORMATION memoryInfo;

    while (VirtualQuery(reinterpret_cast<void *>(baseAddress), &memoryInfo, sizeof(memoryInfo))) {

        // skip noncommitted and guard pages, nonreadable or nonexecutable pages
        if (
            memoryInfo.State & MEM_COMMIT &&
            memoryInfo.Protect == (
                (memoryInfo.Protect & ~(PAGE_NOACCESS | PAGE_GUARD)) &
                (memoryInfo.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
            )
        ) {

            BYTE *res = findPattern(
                static_cast<BYTE *>(memoryInfo.BaseAddress),
                static_cast<BYTE *>(memoryInfo.BaseAddress) + memoryInfo.RegionSize,
                search,
                search_end
            );
            if (res != static_cast<BYTE *>(memoryInfo.BaseAddress) + memoryInfo.RegionSize && res != search) {
                // found
                return res;
            }
        }

        // move to the next region
        baseAddress = reinterpret_cast<ULONG_PTR>(memoryInfo.BaseAddress) + memoryInfo.RegionSize;
    }

    // not found
    return nullptr;
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

BOOL APIENTRY DllMain(HINSTANCE /*hinstDLL*/, const DWORD fdwReason, LPVOID /*lpvReserved*/) {

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
            ULONG ACLEntries[1] = {};
            // the second parameter controls which threads are affected by the hook - use the following values depending on the way the dll is injected:
            // 0 - if the dll is loaded via the main thread (i.e. by manipulating the import table of the process)
            // 1 - if the dll is injected using a throwaway thread (i.e. via CreateRemoteThread and LoadLibraryA)
            LhSetExclusiveACL(ACLEntries, 0, &hHook);
            writeLog("INFO", L"Hooked gethostbyname.");
        }

        const HANDLE currentProcess = GetCurrentProcess();

        const BYTE search[] = {0x89, 0x55, 0x88, 0x83, 0x7D, 0x88, 0x08, 0x74, 0x08};
        constexpr BYTE patch[] = {0xEB, 0x46, 0x90, 0x90};

        BYTE *addressToModify = findPatternInProcessMemory(search, search + 8);
        if (addressToModify == nullptr) {
            writeLog("SEVERE", L"Failed to patch the certificate check: its code was not found. Online gameplay might not be possible.");
        } else {
            SIZE_T bytesWritten;
            if (WriteProcessMemory(currentProcess, addressToModify + 3, patch, sizeof(patch), &bytesWritten)) {
                writeLog("INFO", L"Patched the certificate check.");
            } else {
                writeLog("SEVERE", L"Failed to patch the certificate check: the write failed. Online gameplay might not be possible.");
            }
        }

        closeLog();

    } else if (fdwReason == DLL_PROCESS_DETACH) {
        LhUninstallHook(&hHook);
        LhWaitForPendingRemovals();
    }

    return true;
}
