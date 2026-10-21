#pragma once

// Lightning as drawn and heard: while the shown weather thunders, strikes
// come at random (lightning_style), each a bolt down to the horizon or a
// flash inside the cloud, flickering in a few pulses, its thunder heard once
// the sound has come the strike's distance. None strike while `allowed` is
// false (aiming or a shot in the air), so nothing flashes in play.
// Presentation only, kept by core/weather_frame.h on frame dt. GL-free.

#include "game/lighting_loader.h"
#include "renderer/render_data.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

struct lightning_strike {
    float age = 0.0f;                         // seconds since it struck
    glm::vec3 direction{0.0f, 1.0f, 0.0f};    // unit, from the camera to the top of its bolt
    float seed = 0.0f;                        // its bolt's shape and its pulses
    bool bolt = true;                         // false: a flash inside the cloud only
};

// Thunder on its way: heard in `delay` seconds, the close clap or the far rumble.
struct thunder_clap {
    float delay = 0.0f;
    bool close = false;
};

struct lightning_presentation {
    std::uint32_t random = 0x9E3779B9u;
    float until_next = 0.0f;  // seconds to the next strike
    std::optional<lightning_strike> strike;
    std::vector<thunder_clap> coming;
    // Heard this frame.
    int close_claps = 0;
    int far_claps = 0;
};

// One frame of `dt` seconds at `thunder` (0..1, the shown weather's).
lightning_presentation update_lightning(lightning_presentation state, float thunder, bool allowed,
                                        const lightning_style& style, float dt);

// How bright a strike `age` seconds old flashes, 0..1: a few pulses.
float lightning_flash(float age, float seed, const lightning_style& style);

// What the sky draws of it this frame.
render_lightning make_render_lightning(const lightning_presentation& state, const lightning_style& style);
