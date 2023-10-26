#include <winsock2.h>
#include <easyhook.h>

using namespace std;

__forceinline static BYTE* findPattern(BYTE* srcStart, BYTE* srcEnd, BYTE* patternStart, BYTE* patternEnd) {

    BYTE *pos, *end, *s1, *p1;
    end = srcEnd - (patternEnd - patternStart);

    for (pos = srcStart; pos <= end; pos++) {
        s1 = pos - 1;
        p1 = patternStart - 1;

        while (*++s1 == *++p1) {
            if (p1 == patternEnd) {
                return pos;
            }
        }
    }

    return srcEnd;
}

__forceinline static BYTE* findPatternInProcessMemory(BYTE* search, BYTE* search_end) {

    // start searching from the beginning of the process's memory
    ULONG_PTR baseAddress = (ULONG_PTR) GetModuleHandleA(0);

    MEMORY_BASIC_INFORMATION memoryInfo;
    BYTE* res;

    while (VirtualQuery((void*) baseAddress, &memoryInfo, sizeof(memoryInfo))) {

        // skip noncommitted and guard pages, nonreadable or nonexecutable pages
        if ((memoryInfo.State & MEM_COMMIT) && (memoryInfo.Protect == ((memoryInfo.Protect & ~(PAGE_NOACCESS | PAGE_GUARD)) & (memoryInfo.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))))) {

            res = findPattern((BYTE*) memoryInfo.BaseAddress, (BYTE*) memoryInfo.BaseAddress + memoryInfo.RegionSize, search, search_end);
            if (res != (BYTE*) memoryInfo.BaseAddress + memoryInfo.RegionSize && res != search) {
                // found
                return res;
            }
        }

        // move to the next region
        baseAddress = (ULONG_PTR) ((ULONG_PTR) memoryInfo.BaseAddress + (ULONG_PTR) memoryInfo.RegionSize);
    }

    // not found
    return nullptr;
}

// See: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-gethostbyname
// hostent *WSAAPI gethostbyname(
//   const char *name
// );
hostent *WSAAPI Hooked_gethostbyname(const char *name);

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

HOOK_TRACE_INFO hHook = { NULL }; // keep track of our hook

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {

    if (fdwReason == DLL_PROCESS_ATTACH) {

        NTSTATUS result = LhInstallHook(
            GetProcAddress(GetModuleHandle(TEXT("ws2_32")), "gethostbyname"),
            Hooked_gethostbyname,
            NULL,
            &hHook
        );
        if (FAILED(result)) {
            // Hook could not be installed, see RtlGetLastErrorString() for details
            MessageBoxW(NULL, L"Failed to hook gethostbyname. Online gameplay might not be possible.", L"Error", MB_OK);
            return true;
        }

        ULONG ACLEntries[1] = { 0 };
        LhSetExclusiveACL(ACLEntries, 1, &hHook);

        HANDLE currentProcess = GetCurrentProcess();

        BYTE search[] = { 0x89, 0x55, 0x88, 0x83, 0x7D, 0x88, 0x08, 0x74, 0x08 };
        BYTE patch[] = { 0xEB, 0x46, 0x90, 0x90 };

        BYTE* addressToModify = findPatternInProcessMemory(search, search + 8);
        if(addressToModify) {

            SIZE_T bytesWritten;
            bool certPatchSuccessful = WriteProcessMemory(currentProcess, addressToModify + 3, patch, sizeof(patch), &bytesWritten);
            if(certPatchSuccessful) {
                return true;
            }
        }

        MessageBoxW(NULL, L"Failed to patch certificate. Online gameplay might not be possible.", L"Error", MB_OK);

    } else if (fdwReason == DLL_PROCESS_DETACH) {
        LhUninstallHook(&hHook);
        LhWaitForPendingRemovals();
    }

    return true;
}
