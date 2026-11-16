#pragma once

#include <cstdint>

// Built only with GAME_PATCHER_FRAME_PACING_TRACE (features.h); frame_trace.cpp says what is recorded.
namespace instrumentation {
    namespace frame_trace {

        // game thread, once a pass, when its client update starts: the camera's step scale and whether the pass advances a sub-frame
        void pass(int64_t at, float scale, bool advances);

        // the render thread's worker: a picture the picture pacer gave a refresh is ready (its pass's start, its refresh)
        void paced(int64_t passStart, int64_t slot);

        // the render thread's worker, around each Present it runs (after any hold)
        void presented(int64_t before, int64_t after);
    }
}
