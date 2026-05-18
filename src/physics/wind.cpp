#include "physics/wind.h"

#include <cmath>

wind_state sample_wind(const std::uint32_t seed, const float time) {
    const float phase = static_cast<float>(seed % 1024U) * 0.01f;
    const float speed = 2.0f + 1.5f * std::sin(time * 0.2f + phase);
    const float angle = 0.7f * std::sin(time * 0.11f + phase * 1.3f) + phase;

    wind_state out;
    out.velocity = {
        std::cos(angle) * speed,
        0.0f,
        std::sin(angle) * speed
    };
    return out;
}
