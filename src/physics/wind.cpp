#include "physics/wind.h"

#include <cmath>

wind_state sample_wind(const std::uint32_t seed, const float time, const wind_tuning& tuning) {
    const float phase = static_cast<float>(seed % 1024U) * tuning.seed_phase_scale;
    const float speed = tuning.base_speed
        + tuning.speed_variation * std::sin(time * tuning.speed_time_scale + phase);
    const float angle = tuning.angle_variation
        * std::sin(time * tuning.angle_time_scale + phase * tuning.phase_angle_scale)
        + phase;

    wind_state out;
    out.velocity = {
        std::cos(angle) * speed,
        0.0f,
        std::sin(angle) * speed
    };
    return out;
}
