#pragma once

#include <cstdint>

// Puts the uncapped frame rate's pictures on the display's refreshes (picture_pacer.cpp).
namespace picture_pacer {

    // the picture of a pass: the refresh it is to go on screen at (counted by the vblank clock, 0: none) and when its pass began
    struct Picture {
        int64_t slot;
        int64_t passStart;
    };

    // game thread, when a pass's client update begins at `at` (performance counter ticks): gives the pass's picture its refresh.
    // False while the pictures are not held to their refreshes (no clock, no render thread); otherwise `step` is how far the camera
    // is to step for it, the time between its refresh and the one of the picture before. The first call starts the clock (not under
    // the loader lock, as the patches are applied).
    bool schedule(int64_t at, int64_t &step);

    // the recorder (game thread), recording a Present: the picture being drawn, once (slot 0 after that)
    Picture take();

    // the worker, at the end of a picture's calls: it is ready
    void ready(const Picture &picture);

    // the worker, between batches (outside the exec lock): holds the ready picture's Present until the refresh before its slot
    void hold();
}
