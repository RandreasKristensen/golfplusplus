#include "game/swing.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

float sample_swing_power(const float elapsed, const float cycle_seconds) {
    if (cycle_seconds <= 0.0f) {
        return 0.0f;
    }
    const float phase = std::fmod(elapsed, cycle_seconds) / cycle_seconds;
    return 0.5f - 0.5f * std::cos(phase * glm::two_pi<float>());
}

float swing_meter_power(const float elapsed, const float timing_speed, const float cycle_seconds) {
    return sample_swing_power(elapsed * timing_speed, cycle_seconds);
}
