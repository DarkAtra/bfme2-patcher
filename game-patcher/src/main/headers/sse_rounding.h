#pragma once

#include <cstdint>

#include "patch_set.h"

bool applySseRounding();

namespace sse_rounding {

    // Emit a cdecl double(double) function into the cave and return its address. Values the SSE code does not handle are
    // passed on to the function stored in the dword at `fallbackSlot`.
    // counter and allbackCounter, when not 0, are addresses of dwords incremented on every call and on every call
    // handed to the original function.
    // control, when not 0, is the address of a dword; while it is below 1 the original function runs instead.
    uintptr_t emitFloor(patching::Cave &cave, uintptr_t fallbackSlot, uintptr_t counter = 0, uintptr_t fallbackCounter = 0, uintptr_t control = 0);
    uintptr_t emitCeil(patching::Cave &cave, uintptr_t fallbackSlot, uintptr_t counter = 0, uintptr_t fallbackCounter = 0, uintptr_t control = 0);

    // The square root, for arguments of +0.0 and above. Negative numbers and NaN go to the original function.
    uintptr_t emitSqrt(patching::Cave &cave, uintptr_t fallbackSlot, uintptr_t counter = 0, uintptr_t fallbackCounter = 0, uintptr_t control = 0);
}
