#include "renderer/prop_light.h"

#include "renderer/scattered_light.h"

#include <algorithm>
#include <cstddef>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// lighting.glsl's sun_wrap.
constexpr float sun_wrap = 0.25f;

float wrapped(const glm::vec3& normal, const glm::vec3& towards) {
    return std::max((glm::dot(normal, towards) + sun_wrap) / (1.0f + sun_wrap), 0.0f);
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
        const reaching_light reaching = light_reaching(block, i, world);
        light += reaching.light * wrapped(n, reaching.towards);
    }
    return glm::max(light, glm::vec3(glow));
}
