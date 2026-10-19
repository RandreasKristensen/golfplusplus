#include "renderer/shadow_map_fit.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace {
// The map's depth reaches this many extents either side of its middle along
// the sun: past every caster and the long strip of ground a low sun lights.
constexpr float depth_reach = 4.0f;
// Past this the sun counts as overhead and the light's "up" is north.
constexpr float overhead_sine = 0.999f;
}

glm::mat3 shadow_light_rotation(const glm::vec3& towards_sun) {
    const glm::vec3 z = safe_normalize(towards_sun, world_up);
    const glm::vec3 reference = std::abs(z.y) > overhead_sine ? glm::vec3(0.0f, 0.0f, -1.0f) : world_up;
    const glm::vec3 x = glm::normalize(glm::cross(reference, z));
    const glm::vec3 y = glm::cross(z, x);
    // Rows x, y, z: glm is column-major, so build the transpose.
    return glm::transpose(glm::mat3(x, y, z));
}

shadow_map_fit fit_shadow_map(const glm::vec3& towards_sun, const glm::vec3& eye, const glm::vec3& forward,
                              const float extent, const int resolution, const float ahead) {
    const glm::mat3 rotation = shadow_light_rotation(towards_sun);
    const glm::vec3 level_forward = safe_normalize(horizontal(forward), glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 middle = rotation * (eye + level_forward * (extent * ahead));
    const float texel = extent / static_cast<float>(std::max(1, resolution));
    const float half = extent * 0.5f;
    // The corner on the texel grid; the extent is a whole number of texels.
    const float left = std::floor((middle.x - half) / texel) * texel;
    const float bottom = std::floor((middle.y - half) / texel) * texel;
    const float reach = extent * depth_reach;
    // The view looks down -z, so depth runs from the sun's side.
    const glm::mat4 proj = glm::ortho(left, left + extent, bottom, bottom + extent, -(middle.z + reach), -(middle.z - reach));
    shadow_map_fit fit;
    fit.view_proj = proj * glm::mat4(rotation);
    fit.depth_range = 2.0f * reach;
    return fit;
}
