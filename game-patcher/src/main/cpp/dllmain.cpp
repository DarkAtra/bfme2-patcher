#include <windows.h>
#include <winsock.h>
#include "detours.h"

using namespace std;

static wchar_t* charToWChar(const char* text)
{
    const size_t size = strlen(text) + 1;
    wchar_t* wText = new wchar_t[size];
    mbstowcs(wText, text, size);
    return wText;
}

typedef hostent *(__stdcall *Real_gethostbyname)(const char *a0);

static Real_gethostbyname Orig_gethostbyname = NULL;

hostent *Hooked_gethostbyname(const char *name) {

    MessageBoxW(NULL, L"Test", L"Test", MB_OK);

    if (strcmp("gpcm.gamespy.com", name) == 0) {
        return Orig_gethostbyname("gpcm.server.cnc-online.net");
    } else if (strcmp("peerchat.gamespy.com", name) == 0) {
        return Orig_gethostbyname("peerchat.server.cnc-online.net");
    } else if (strcmp("psweb.gamespy.com", name) == 0) {
        return Orig_gethostbyname("server.cnc-online.net");
    } else if (strcmp("arenasdk.gamespy.com", name) == 0) {
        return Orig_gethostbyname("server.cnc-online.net");
    } else if (strcmp("lotrbfme.arenasdk.gamespy.com", name) == 0) {
        return Orig_gethostbyname("server.cnc-online.net");
    } else if (strcmp("ingamead.gamespy.com", name) == 0) {
        return Orig_gethostbyname("server.cnc-online.net");
    } else if (strcmp("master.gamespy.com", name) == 0) {
        return Orig_gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme.available.gamespy.com", name) == 0) {
        return Orig_gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme.master.gamespy.com", name) == 0) {
        return Orig_gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme.ms13.gamespy.com", name) == 0) {
        return Orig_gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.available.gamespy.com", name) == 0) {
        return Orig_gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.master.gamespy.com", name) == 0) {
        return Orig_gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.ms9.gamespy.com", name) == 0) {
        return Orig_gethostbyname("master.server.cnc-online.net");
    } else if (strcmp("gamestats.gamespy.com", name) == 0) {
        return Orig_gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("lotrbme.gamestats.gamespy.com", name) == 0) {
        return Orig_gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("lotrbme2r.gamestats.gamespy.com", name) == 0) {
        return Orig_gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("lotrbme2wk.gamestats.gamespy.com", name) == 0) {
        return Orig_gethostbyname("gamestats2.server.cnc-online.net");
    } else if (strcmp("servserv.generals.ea.com", name) == 0) {
        return Orig_gethostbyname("http.server.cnc-online.net");
    } else if (strcmp("na.llnet.eadownloads.ea.com", name) == 0) {
        return Orig_gethostbyname("http.server.cnc-online.net");
    } else if (strcmp("bfme.fesl.ea.com", name) == 0) {
        return Orig_gethostbyname("login.server.cnc-online.net");
    } else if (strcmp("bfme2.fesl.ea.com", name) == 0) {
        return Orig_gethostbyname("login.server.cnc-online.net");
    } else if (strcmp("bfme2-ep1-pc.fesl.ea.com", name) == 0) {
        return Orig_gethostbyname("login.server.cnc-online.net");
    }

    return Orig_gethostbyname(name);
}

bool __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {

    if (DetourIsHelperProcess()) {
        return true;
    }

    if (fdwReason == DLL_PROCESS_ATTACH) {

        MessageBoxW(NULL, L"Hello", L"Hello", MB_OK);

        DetourRestoreAfterWith();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        Orig_gethostbyname = (Real_gethostbyname)GetProcAddress(GetModuleHandleW(L"ws2_32"), "gethostbyname");

        DetourAttach(&(PVOID &) Orig_gethostbyname, Hooked_gethostbyname);
        if(DetourTransactionCommit() == NO_ERROR) {
            MessageBoxW(NULL, L"Hooked", L"Hooked", MB_OK);
        }
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID &) Orig_gethostbyname, Hooked_gethostbyname);
        if(DetourTransactionCommit() == NO_ERROR) {
            MessageBoxW(NULL, L"Unhooked", L"Unhooked", MB_OK);
        }
    }

    return true;
}
