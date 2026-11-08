#include <windows.h>
#include <cstdint>

#include "os_name_fallback.h"
#include "patch_set.h"

// OS name fallback
//
// Problem: without the Windows XP compatibility mode the game crashes before its first frame. The startup hardware report
// (0x00A26C50) names the Windows version through 0x00A25630, which knows the NT major versions 4 and 5 and returns without
// writing the name for any other; the report then prints the uninitialised pointer with "%s\t" (0x00A26C77). Windows 10 and 11
// report major 10 to a process without the compatibility mode; the mode avoids the crash only by reporting 5.1.
// Fix: an NT major other than 4 and 5 takes the major 5 arm's last case, which names it "WINXX", the name the engine already gives
// an NT version it does not list, and returns through the same four pops.
//
// The other readers of the version (the GetVersionExA calls) only test for NT, for major 5 and up, for Windows 2000 or for
// Windows 9x, which Windows 10 answers as the compatibility mode's 5.1 does. Derived from pySAGE's os-name-fallback
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t MAJOR_TEST = 0x00A256A3;
    constexpr uintptr_t OTHER_MAJOR_BRANCH = 0x00A256A6;
    constexpr uintptr_t OTHER_NAME_CASE = 0x00A256CB;
    constexpr uintptr_t BARE_RETURN = 0x00A257B1;
}

bool applyOsNameFallback() {

    static PatchSet set;
    set.reset();

    // cmp edi, 5: the major version
    const BYTE majorTest[3] = {0x83, 0xFF, 0x05};
    set.expect(MAJOR_TEST, majorTest);

    // jne to the bare return, now to the "WINXX" case
    const BYTE toBareReturn[6] = {0x0F, 0x85, 0x05, 0x01, 0x00, 0x00};
    const BYTE toOtherName[6] = {0x0F, 0x85, 0x1F, 0x00, 0x00, 0x00};
    set.replace(OTHER_MAJOR_BRANCH, toBareReturn, toOtherName);

    // pop edi / pop esi / mov dword [ecx], "WINXX" / pop ebx / pop ecx / ret
    const BYTE otherName[11] = {0x5F, 0x5E, 0xC7, 0x01, 0xE8, 0x45, 0xC9, 0x00, 0x5B, 0x59, 0xC3};
    set.expect(OTHER_NAME_CASE, otherName);

    // pop edi / pop esi / pop ebx / pop ecx / ret: the stock target, the same pops without the name
    const BYTE bareReturn[5] = {0x5F, 0x5E, 0x5B, 0x59, 0xC3};
    set.expect(BARE_RETURN, bareReturn);

    const BYTE winxx[6] = {'W', 'I', 'N', 'X', 'X', 0x00};
    set.expect(0x00C945E8, winxx);

    return commit(set, Cave(nullptr));
}
