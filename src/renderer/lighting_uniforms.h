#pragma once

// The `frame_lighting` uniform block (assets/shaders/lighting.glsl): the
// scene's light and haze, uploaded once per frame into one buffer that every
// scene shader reads. shader_program binds the block of every program that
// has it to frame_lighting_binding when it links.

#include "renderer/render_data.h"
#include "renderer/scene_haze.h"
#include "renderer/scene_lights.h"
#include "renderer/sky_visibility.h"

#include <array>
#include <cstddef>
#include <vector>

#include <glm/vec4.hpp>

inline constexpr const char* frame_lighting_block_name = "frame_lighting";
inline constexpr unsigned int frame_lighting_binding = 0;

// std140, field for field as lighting.glsl declares it.
struct frame_lighting_block {
    glm::vec4 sun_direction{0.0f, 1.0f, 0.0f, 0.0f};  // w: star visibility
    glm::vec4 sun_color{0.0f};
    glm::vec4 sky_light{0.0f};
    glm::vec4 ground_light{0.0f};
    glm::vec4 sky_zenith{0.0f};  // a: sky_blend
    glm::vec4 haze_color{0.0f};  // a: far_amount
    glm::vec4 haze_eye{0.0f};    // w: 1 / full_distance
    glm::vec4 moon_direction{0.0f, -1.0f, 0.0f, 0.0f};  // w: moon_phase
    glm::vec4 horizon_glow{0.0f};                       // a: horizon_glow_strength
    glm::vec4 sun_horizon_a{0.0f};  // horizon_way_weights, ways 0..3
    glm::vec4 sun_horizon_b{0.0f};  // and 4..7
    glm::vec4 shadow{0.0f};         // render_shadows: sun_share, night_share, max_stretch, horizon_softness
    glm::vec4 lamp_glow{0.0f};
    std::array<glm::vec4, max_scene_lights> light_position{};  // w: radius, 0 for none
    std::array<glm::vec4, max_scene_lights> light_color{};     // w: cone_full_cos
    std::array<glm::vec4, max_scene_lights> light_cone{};      // xyz: direction; w: cone_edge_cos
};
static_assert(sizeof(frame_lighting_block) == (13 + 3 * max_scene_lights) * sizeof(glm::vec4),
              "frame_lighting_block must match std140");

// `lights` are the ones the scene is lit by (nearest_scene_lights); any past
// max_scene_lights are left out.
inline frame_lighting_block make_frame_lighting_block(const render_lighting& lighting, const scene_haze& haze,
                                                      const std::vector<render_light>& lights) {
    frame_lighting_block block;
    block.sun_direction = glm::vec4(lighting.sun_direction, lighting.star_visibility);
    block.sun_color = glm::vec4(lighting.sun_color, 0.0f);
    block.sky_light = glm::vec4(lighting.ambient_sky, 0.0f);
    block.ground_light = glm::vec4(lighting.ambient_ground, 0.0f);
    block.sky_zenith = glm::vec4(lighting.sky_zenith, lighting.sky_blend);
    block.haze_color = glm::vec4(haze.color, haze.far_amount);
    block.haze_eye = glm::vec4(haze.eye, 1.0f / haze.full_distance);
    block.moon_direction = glm::vec4(lighting.moon_direction, lighting.moon_phase);
    block.horizon_glow = glm::vec4(lighting.horizon_glow, lighting.horizon_glow_strength);
    const std::array<float, horizon_way_count> ways = horizon_way_weights(lighting.sun_direction);
    block.sun_horizon_a = glm::vec4(ways[0], ways[1], ways[2], ways[3]);
    block.sun_horizon_b = glm::vec4(ways[4], ways[5], ways[6], ways[7]);
    const render_shadows& shadows = lighting.shadows;
    block.shadow = glm::vec4(shadows.sun_share, shadows.night_share, shadows.max_stretch, shadows.horizon_softness);
    block.lamp_glow = glm::vec4(lighting.lamp_glow, 0.0f);
    for (std::size_t i = 0; i < max_scene_lights; ++i) {
        const render_light light = i < lights.size() ? lights[i] : render_light{};
        block.light_position[i] = glm::vec4(light.position, light.radius);
        block.light_color[i] = glm::vec4(light.color, light.cone_full_cos);
        block.light_cone[i] = glm::vec4(light.direction, light.cone_edge_cos);
    }
    return block;
}

class lighting_uniforms {
public:
    bool init();
    void shutdown();
    // Writes this frame's light, lit by the lights nearest the camera, and
    // leaves the buffer bound to frame_lighting_binding.
    void upload(const render_lighting& lighting, const scene_haze& haze, const std::vector<render_light>& lights,
                frame_profile* profile);

private:
    unsigned int buffer_ = 0;
};
