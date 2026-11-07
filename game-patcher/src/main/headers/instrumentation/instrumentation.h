#pragma once

#include <cstddef>

#include "patches.h"

namespace instrumentation {

    // Applies the measuring tools (debug mode). Failures are added to `failed` like the patches'.
    void apply(const wchar_t *(&failed)[MAX_FAILED_PATCHES], size_t &count);
}
