#pragma once

#include <windows.h>
#include <cwchar>

// Whether the game was started with `argument` (e.g. L"-showfps"), compared without case and outside quotes. The game's command
// line parser skips arguments it does not know (0x007BA7E1), so the game itself ignores the ones game-patcher reads.
inline bool hasArgument(const wchar_t *argument) {
    const size_t length = wcslen(argument);
    bool quoted = false;
    bool argumentStart = true;
    for (const wchar_t *position = GetCommandLineW(); *position != L'\0'; position++) {
        if (*position == L'"') {
            quoted = !quoted;
            argumentStart = false;
            continue;
        }
        if (!quoted && (*position == L' ' || *position == L'\t')) {
            argumentStart = true;
            continue;
        }
        if (argumentStart && !quoted && _wcsnicmp(position, argument, length) == 0) {
            const wchar_t end = position[length];
            if (end == L'\0' || end == L' ' || end == L'\t') {
                return true;
            }
        }
        argumentStart = false;
    }
    return false;
}
