#include "game/swing.h"

#include <cmath>

float sample_swing_power(const float elapsed) {
    constexpr float pi = 3.14159265358979323846f;
    constexpr float cycle_seconds = 1.4f;
    const float phase = std::fmod(elapsed, cycle_seconds) / cycle_seconds;
    return 0.5f - 0.5f * std::cos(phase * 2.0f * pi);
}
