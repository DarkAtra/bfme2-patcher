#pragma once

#include <cstdint>

namespace instrumentation {

    bool applyCallTiming();

    // Moves the start of every open call timer forward by `cycles`, so that time the instrumentation spends on its own work inside
    // them is not counted.
    void pauseCallTimers(uint64_t cycles);
}
