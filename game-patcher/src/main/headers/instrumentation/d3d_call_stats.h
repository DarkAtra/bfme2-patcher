#pragma once

#include <cstdint>

namespace instrumentation {

    bool applyD3dCallStats();

    // call_timing.cpp hands over the device and the effect vtable it hooks once the game renders; without -d3dstats these do nothing
    void d3dCallStatsDevice(uint32_t device);
    void d3dCallStatsEffect(uint32_t vtable);

    // with -d3dsim: the render thread simulation runs, and call_timing leaves its own D3D wrappers out
    bool d3dCallStatsSimulates();

    // call_timing.cpp brackets each engine update (one frame) with these
    void d3dCallStatsFrame(bool begin);
}
