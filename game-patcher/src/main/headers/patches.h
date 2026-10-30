#pragma once

#include <cstddef>

constexpr size_t MAX_FAILED_PATCHES = 32;

// Applies every patch that features.h enables. Each patch is applied as a whole or not at all, and one failing does not
// stop the others. Returns how many failed and stores their names in `failed`.
size_t applyPatches(const wchar_t *(&failed)[MAX_FAILED_PATCHES]);
