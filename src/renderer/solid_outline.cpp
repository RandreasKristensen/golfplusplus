#include "renderer/solid_outline.h"

#include "physics/vector_math.h"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
constexpr int ring_segments = 12;
constexpr int ring_uprights = 4;

void add_ring(std::vector<world_segment>& out, const glm::vec3& centre, const float radius) {
    for (int i = 0; i < ring_segments; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ring_segments);
        const float b = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(ring_segments);
        out.emplace_back(centre + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * radius,
                         centre + glm::vec3(std::cos(b), 0.0f, std::sin(b)) * radius);
    }
}

// A box's four foot corners, then its four top corners, round its middle.
void add_box(std::vector<world_segment>& out, const glm::vec3& base, const glm::vec3& along, const glm::vec3& across,
             const float height) {
    const glm::vec3 up = world_up * height;
    const glm::vec3 corners[4] = {base + along + across, base + along - across, base - along - across, base - along + across};
    for (int i = 0; i < 4; ++i) {
        const glm::vec3& a = corners[i];
        const glm::vec3& b = corners[(i + 1) % 4];
        out.emplace_back(a, b);
        out.emplace_back(a + up, b + up);
        out.emplace_back(a, a + up);
    }
}
}

std::vector<world_segment> solid_outline(const solid& thing) {
    std::vector<world_segment> out;
    const glm::vec3 top = thing.base + world_up * thing.height;
    switch (thing.shape) {
    case solid_shape::cylinder:
        add_ring(out, thing.base, thing.radius);
        add_ring(out, top, thing.radius);
        for (int i = 0; i < ring_uprights; ++i) {
            const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ring_uprights);
            const glm::vec3 side = glm::vec3(std::cos(a), 0.0f, std::sin(a)) * thing.radius;
            out.emplace_back(thing.base + side, top + side);
        }
        break;
    case solid_shape::cone:
        add_ring(out, thing.base, thing.radius);
        for (int i = 0; i < ring_uprights; ++i) {
            const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ring_uprights);
            out.emplace_back(thing.base + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * thing.radius, top);
        }
        break;
    case solid_shape::wall: {
        const glm::vec3 across = safe_normalize(glm::cross(horizontal(thing.end - thing.base), world_up), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 middle = 0.5f * (thing.base + thing.end);
        add_box(out, middle, 0.5f * (thing.end - thing.base), across * thing.radius, thing.height);
        break;
    }
    case solid_shape::box: {
        const glm::vec3 along = safe_normalize(horizontal(thing.along), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::vec3 across = glm::cross(world_up, along);
        add_box(out, thing.base, along * thing.half_length, across * thing.half_width, thing.height);
        break;
    }
    }
    return out;
}
