#include "game/sun_lighting.h"

#include "game/lighting_fields.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glm/common.hpp>

namespace {
// 0 with the sun at `none`, 1 at `full` (either side of it), smooth between.
float elevation_share(const float none, const float full, const float sun_elevation_deg) {
    const float t = glm::clamp((sun_elevation_deg - none) / (full - none), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

lighting_key lighting_at(const lighting_palette& palette, const float sun_elevation_deg) {
    const std::vector<lighting_key>& keys = palette.keys;
    if (keys.empty()) {
        return lighting_key{};
    }
    if (sun_elevation_deg <= keys.front().sun_elevation_deg) {
        return keys.front();
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const lighting_key& low = keys[i - 1];
        const lighting_key& high = keys[i];
        if (sun_elevation_deg < high.sun_elevation_deg) {
            const float t = (sun_elevation_deg - low.sun_elevation_deg) / (high.sun_elevation_deg - low.sun_elevation_deg);
            lighting_key key;
            key.sun_elevation_deg = sun_elevation_deg;
            key.exposure = glm::mix(low.exposure, high.exposure, t);
            for (const color_field<lighting_key>& field : key_fields) {
                key.*field.member = glm::mix(low.*field.member, high.*field.member, t);
            }
            for (const share_field<lighting_key>& field : key_share_fields) {
                key.*field.member = glm::mix(low.*field.member, high.*field.member, t);
            }
            return key;
        }
    }
    return keys.back();
}

float star_visibility(const sky_style& sky, const float sun_elevation_deg) {
    if (!(sky.stars_full_deg < sky.stars_appear_deg)) {
        return sun_elevation_deg < sky.stars_appear_deg ? 1.0f : 0.0f;
    }
    return elevation_share(sky.stars_appear_deg, sky.stars_full_deg, sun_elevation_deg);
}

float sun_shadow_share(const shadow_style& shadows, const float sun_elevation_deg) {
    if (!(shadows.sun_fade_start_deg < shadows.sun_fade_full_deg)) {
        return sun_elevation_deg >= shadows.sun_fade_full_deg ? 1.0f : 0.0f;
    }
    return elevation_share(shadows.sun_fade_start_deg, shadows.sun_fade_full_deg, sun_elevation_deg);
}

float night_light_share(const night_light_style& lights, const float sun_elevation_deg) {
    if (!(lights.full_deg < lights.on_deg)) {
        return sun_elevation_deg <= lights.full_deg ? 1.0f : 0.0f;
    }
    return elevation_share(lights.on_deg, lights.full_deg, sun_elevation_deg);
}

glow_floors night_glow_floors(const glow_floors& day, const night_light_style& lights, const float night_share) {
    glow_floors glow = day;
    glow.ball = glm::mix(day.ball, std::max(day.ball, lights.ball_glow), night_share);
    glow.ember = glm::mix(day.ember, std::max(day.ember, lights.ember_glow), night_share);
    return glow;
}
