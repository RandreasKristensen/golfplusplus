#pragma once

// Lightning as drawn and heard: while the shown weather thunders, strikes
// come at random (lightning_style), each a bolt down to the horizon or a
// flash inside the cloud, flickering in a few pulses, its thunder heard once
// the sound has come the strike's distance. None strike while `allowed` is
// false (aiming or a shot in the air), so nothing flashes in play. A real
// strike (a golfer struck, strike_at) is never held back: its bolt comes down
// on the golfer. Presentation only, kept by core/weather_frame.h on frame dt.
// GL-free.

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
    // A real strike: the world point its bolt comes down on. `direction` is
    // then made from the camera each frame (make_render_lightning).
    std::optional<glm::vec3> point;
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

// A real strike on `point` (world units), seen from `camera`, replacing any
// strike showing, its thunder heard once the sound has come the distance
// (`meters_per_unit` metres a world unit), close or far as a random one's.
lightning_presentation strike_at(lightning_presentation state, const glm::vec3& point, const glm::vec3& camera,
                                 float meters_per_unit, const lightning_style& style);

// How bright a strike `age` seconds old flashes, 0..1: a few pulses.
float lightning_flash(float age, float seed, const lightning_style& style);

// What the sky draws of it this frame; `camera` is where the scene is seen
// from, which a real strike's bolt comes down towards the point from.
render_lightning make_render_lightning(const lightning_presentation& state, const lightning_style& style,
                                       const glm::vec3& camera = glm::vec3(0.0f));
