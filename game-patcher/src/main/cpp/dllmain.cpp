#include <winsock2.h>
#include <easyhook.h>

using namespace std;

// See: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-gethostbyname
// hostent *WSAAPI gethostbyname(
//   const char *name
// );
hostent *WSAAPI Hooked_gethostbyname(const char *name);

hostent *WSAAPI Hooked_gethostbyname(const char *name) {

    MessageBoxW(NULL, L"Test", L"Test", MB_OK);

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

        MessageBoxW(NULL, L"Hello", L"Hello", MB_OK);

        NTSTATUS result = LhInstallHook(
            GetProcAddress(GetModuleHandle(TEXT("ws2_32")), "gethostbyname"),
            Hooked_gethostbyname,
            NULL,
            &hHook
        );
        if (FAILED(result)) {
            // Hook could not be installed, see RtlGetLastErrorString() for details
            MessageBoxW(NULL, L"Error", L"Error", MB_OK);
            return true;
        }

        ULONG ACLEntries[1] = { 0 };
        LhSetExclusiveACL(ACLEntries, 1, &hHook);

        MessageBoxW(NULL, L"Hooked", L"Hooked", MB_OK);

    } else if (fdwReason == DLL_PROCESS_DETACH) {
        LhUninstallHook(&hHook);
        LhWaitForPendingRemovals();
        MessageBoxW(NULL, L"Unhooked", L"Unhooked", MB_OK);
    }

    return true;
}
