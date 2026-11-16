#pragma once

bool applyRenderRatePatch();

// the camera's step scale render_rate applies, 0.5 at its 60 frames a second; nullptr while the patch is not applied
float *renderRateScrollScale();
