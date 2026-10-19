#include "renderer/floodlight_batch.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
constexpr int pole_sides = 6;
// Poles are drawn at least this thick, so the low-res target keeps them as a
// pixel instead of losing them between scanlines.
constexpr float min_drawn_pole_radius = 0.12f;
// The lamp head: across, up and down, and front to back, in metres; the arm
// holding it.
constexpr float head_width = 1.6f;
constexpr float head_height = 0.9f;
constexpr float head_depth = 0.4f;
constexpr float arm_half_width = 0.07f;

void append_quad(std::vector<floodlight_vertex>& out, const std::array<glm::vec3, 4>& corners, const glm::vec3& normal,
                 const glm::vec3& color, const float lamp) {
    const auto vertex = [&](const glm::vec3& p) { return floodlight_vertex{p, normal, color, lamp}; };
    out.insert(out.end(), {vertex(corners[0]), vertex(corners[1]), vertex(corners[2]),
                           vertex(corners[0]), vertex(corners[2]), vertex(corners[3])});
}

void append_pole(std::vector<floodlight_vertex>& out, const glm::vec3& foot, const float radius, const float height,
                 const glm::vec3& color) {
    const glm::vec3 up = world_up * height;
    for (int i = 0; i < pole_sides; ++i) {
        const float a0 = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(pole_sides);
        const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(pole_sides);
        const glm::vec3 p0 = foot + glm::vec3(std::cos(a0), 0.0f, std::sin(a0)) * radius;
        const glm::vec3 p1 = foot + glm::vec3(std::cos(a1), 0.0f, std::sin(a1)) * radius;
        const float middle = (a0 + a1) * 0.5f;
        append_quad(out, {p0, p0 + up, p1 + up, p1}, glm::vec3(std::cos(middle), 0.0f, std::sin(middle)), color, 0.0f);
    }
}

// A box round `center` with half sizes `half` along the unit axes `x`, `y`,
// `z`; when `lit_front` its +z face and its -y face (the lens and its rim,
// seen from below at any angle) glow.
void append_box(std::vector<floodlight_vertex>& out, const glm::vec3& center, const glm::vec3& x, const glm::vec3& y,
                const glm::vec3& z, const glm::vec3& half, const glm::vec3& color, const glm::vec3& front_color,
                const bool lit_front) {
    const glm::vec3 axes[3] = {x * half.x, y * half.y, z * half.z};
    for (int axis = 0; axis < 3; ++axis) {
        const glm::vec3& normal_axis = axes[axis];
        const glm::vec3& u = axes[(axis + 1) % 3];
        const glm::vec3& v = axes[(axis + 2) % 3];
        for (const float side : {1.0f, -1.0f}) {
            const glm::vec3 face = center + normal_axis * side;
            const glm::vec3 normal = safe_normalize(normal_axis * side, world_up);
            const bool front = (axis == 2 && side > 0.0f) || (axis == 1 && side < 0.0f);
            append_quad(out, {face - u - v, face + u - v, face + u + v, face - u + v}, normal, front ? front_color : color,
                        front && lit_front ? 1.0f : 0.0f);
        }
    }
}

void append_floodlight(std::vector<floodlight_vertex>& out, const floodlight& light, const fixture_colors& colors) {
    const float radius = std::max(min_drawn_pole_radius, light.radius);
    const glm::vec3 top(light.foot.x, light.lamp.y, light.foot.z);
    append_pole(out, light.foot, radius, light.lamp.y - light.foot.y, colors.floodlight_pole);

    const glm::vec3 aim = safe_normalize(light.aim, -world_up);
    const glm::vec3 across = safe_normalize(glm::cross(aim, world_up), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 head_up = glm::cross(across, aim);
    append_box(out, light.lamp, across, head_up, aim, glm::vec3(head_width, head_height, head_depth) * 0.5f,
               colors.floodlight_head, colors.floodlight_pole, true);

    // The arm from the pole's top to the back of the head.
    const glm::vec3 back = light.lamp - aim * (head_depth * 0.5f);
    const glm::vec3 reach = back - top;
    const float length = glm::length(reach);
    if (length > 0.001f) {
        const glm::vec3 along = reach / length;
        const glm::vec3 side = safe_normalize(glm::cross(along, world_up), glm::vec3(1.0f, 0.0f, 0.0f));
        append_box(out, top + reach * 0.5f, side, glm::cross(side, along), along,
                   glm::vec3(arm_half_width, arm_half_width, length * 0.5f), colors.floodlight_pole,
                   colors.floodlight_pole, false);
    }
}
}

render_floodlights build_render_floodlights(const play_area& area, const std::uint64_t revision, const fixture_colors& colors) {
    render_floodlights floodlights;
    floodlights.revision = revision;
    for (const floodlight& light : area.floodlights) {
        append_floodlight(floodlights.vertices, light, colors);
    }
    return floodlights;
}
