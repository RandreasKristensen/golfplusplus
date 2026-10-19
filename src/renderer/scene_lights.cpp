#include "renderer/scene_lights.h"

#include <algorithm>

#include <glm/geometric.hpp>

std::vector<render_light> nearest_scene_lights(const std::vector<render_light>& lights, const glm::vec3& eye) {
    std::vector<render_light> nearest;
    for (const render_light& light : lights) {
        if (light.radius > 0.0f && glm::dot(light.color, light.color) > 0.0f) {
            nearest.push_back(light);
        }
    }
    const auto outside_reach = [&eye](const render_light& light) {
        return std::max(0.0f, glm::distance(light.position, eye) - light.radius);
    };
    std::stable_sort(nearest.begin(), nearest.end(), [&](const render_light& a, const render_light& b) {
        const float a_outside = outside_reach(a);
        const float b_outside = outside_reach(b);
        if (a_outside != b_outside) {
            return a_outside < b_outside;
        }
        return glm::distance(a.position, eye) < glm::distance(b.position, eye);
    });
    if (nearest.size() > max_scene_lights) {
        nearest.resize(max_scene_lights);
    }
    return nearest;
}
