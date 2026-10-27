#include "renderer/prop_light.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// lighting.glsl's sun_wrap.
constexpr float sun_wrap = 0.25f;

float wrapped(const glm::vec3& normal, const glm::vec3& towards) {
    return std::max((glm::dot(normal, towards) + sun_wrap) / (1.0f + sun_wrap), 0.0f);
}

// As the GPU's: an empty light's equal edges give 0 / 0, which its min and
// max (as fmin and fmax) drop.
float smoothstep(const float edge0, const float edge1, const float x) {
    const float t = std::fmin(std::fmax((x - edge0) / (edge1 - edge0), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

glm::vec3 prop_light(const frame_lighting_block& block, const glm::vec3& world, const glm::vec3& normal, const float sun,
                     const float glow) {
    glm::vec3 n = glm::normalize(normal);
    if (glm::dot(n, glm::vec3(block.haze_eye) - world) < 0.0f) {
        n = -n;
    }
    const glm::vec3 hemisphere = glm::mix(glm::vec3(block.ground_light), glm::vec3(block.sky_light), n.y * 0.5f + 0.5f);
    glm::vec3 light = hemisphere + glm::vec3(block.sun_color) * wrapped(n, glm::vec3(block.sun_direction)) * sun;
    for (std::size_t i = 0; i < block.light_position.size(); ++i) {
        const glm::vec3 to = glm::vec3(block.light_position[i]) - world;
        const float distance_to = glm::length(to);
        const glm::vec3 towards = to / std::max(distance_to, 0.001f);
        const float radius = block.light_position[i].w;
        const float reach = std::clamp(1.0f - distance_to * distance_to / std::max(radius * radius, 0.0001f), 0.0f, 1.0f);
        const float cone = smoothstep(block.light_cone[i].w, block.light_color[i].w, glm::dot(-towards, glm::vec3(block.light_cone[i])));
        light += glm::vec3(block.light_color[i]) * reach * reach * cone * wrapped(n, towards);
    }
    return glm::max(light, glm::vec3(glow));
}
