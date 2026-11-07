#pragma once

#include <windows.h>
#include <cwchar>

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
    if (wcscat_s(path, L".patcher\\") != 0) {
        return 0;
    }
    return wcslen(path);
}
