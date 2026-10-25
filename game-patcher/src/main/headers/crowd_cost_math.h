#pragma once

#include <emmintrin.h>

// The arithmetic the game's crowd cost does on the x87 stack for every unit in a cell window, rounded to the precision of
// the control word: single for 24 bits, double for 53. `Wide` selects the double version; the float versions round every
// operation to float, as 24 bits do.

namespace crowd_cost {

    inline float rootOf(const float value) {
        return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(value)));
    }

    inline double rootOf(const double value) {
        return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(value), _mm_set_sd(value)));
    }

    // A float difference that is neither zero nor between 1e-15 and 1e15 in size could overflow or underflow when squared
    // in float, which the x87 (with its wider exponent) does not; such values never occur as positions on a map.
    inline bool squarable(const float difference) {
        const float size = difference < 0 ? -difference : difference;
        return difference == 0.0f || (size >= 1e-15f && size <= 1e15f);
    }

    // The distance of a unit to the cell centre, stored as a float. `ok` is false for values this does not reproduce.
    template<bool Wide>
    float distanceTo(const float x, const float y, const float centreX, const float centreY, bool &ok) {
        if (Wide) {
            const double dx = static_cast<double>(x) - static_cast<double>(centreX);
            const double dy = static_cast<double>(y) - static_cast<double>(centreY);
            return static_cast<float>(rootOf(dy * dy + dx * dx));
        }
        const float dx = x - centreX;
        const float dy = y - centreY;
        if (!squarable(dx) || !squarable(dy)) {
            ok = false;
        }
        return rootOf(dy * dy + dx * dx);
    }

    // Speeds the float division reproduces: zero (the quotient is infinity in both) or a size that cannot overflow or
    // underflow the quotient of a distance on a map. NaN is not usable.
    inline bool usableSpeed(const float speed) {
        const float size = speed < 0 ? -speed : speed;
        return speed == 0.0f || (size >= 1e-20f && size <= 1e20f);
    }

    // distance / speed, a float for 24 bits and a double for 53 (the game compares it with a float travel time)
    template<bool Wide>
    double timeFor(const float distance, const float speed) {
        if (Wide) {
            return static_cast<double>(distance) / static_cast<double>(speed);
        }
        return static_cast<double>(distance / speed);
    }
}
