#pragma once

#include <cstdint>

#include "patch_set.h"

bool applySseRounding();

namespace sse_rounding {

    // Emit a cdecl double(double) function into the cave and return its address. Values the SSE code does not handle are
    // passed on to the function stored in the dword at `fallbackSlot`.
    uintptr_t emitFloor(patching::Cave &cave, uintptr_t fallbackSlot);
    uintptr_t emitCeil(patching::Cave &cave, uintptr_t fallbackSlot);

    // fabs, for every finite argument. Infinities and NaN go to the original function.
    uintptr_t emitFabs(patching::Cave &cave, uintptr_t fallbackSlot);

    // The square root, for arguments of +0.0 and above. Negative numbers and NaN go to the original function.
    uintptr_t emitSqrt(patching::Cave &cave, uintptr_t fallbackSlot);
}
