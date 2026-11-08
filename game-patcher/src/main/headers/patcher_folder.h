#pragma once

#include <windows.h>
#include <cwchar>

// Appends `tail` to `path` and returns whether it fit. Checks first: wcscat_s does not return an error for a path that does not
// fit, it ends the process through the C runtime's invalid parameter handler.
inline bool appendToPath(wchar_t (&path)[MAX_PATH], const wchar_t *tail) {
    if (wcslen(path) + wcslen(tail) >= MAX_PATH) {
        return false;
    }
    return wcscat_s(path, tail) == 0;
}

// Puts "<game folder>\.patcher\" (next to the updater's logs) into `path` and returns its length, or 0 when it does not fit.
inline size_t patcherFolder(wchar_t (&path)[MAX_PATH]) {
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return 0;
    }
    wchar_t *folderEnd = wcsrchr(path, L'\\');
    if (folderEnd == nullptr) {
        return 0;
    }
    folderEnd[1] = L'\0';
    if (!appendToPath(path, L".patcher\\")) {
        return 0;
    }
    return wcslen(path);
}
