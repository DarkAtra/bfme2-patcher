#pragma once

#include <cstddef>

constexpr size_t MAX_FAILED_PATCHES = 48;

// Applies every patch that features.h enables, and with -debugmode on the game's command line the measuring tools after them
// (instrumentation.cpp). Each patch is applied as a whole or not at all, and one failing does not stop the others. Returns how
// many failed and stores their names in `failed`.
size_t applyPatches(const wchar_t *(&failed)[MAX_FAILED_PATCHES]);

// Applies one patch when `enabled`, records it for the crash dumps and adds its name to `failed` when it fails.
void runPatch(bool enabled, const wchar_t *name, bool (*apply)(), const wchar_t *(&failed)[MAX_FAILED_PATCHES], size_t &count);
