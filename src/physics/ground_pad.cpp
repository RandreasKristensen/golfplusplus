#include "physics/ground_pad.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
glm::vec3 across_of(const ground_pad& pad) {
    return glm::vec3(-pad.along.z, 0.0f, pad.along.x);
}

// How fully `pad` decides the height at `position`: 1 on it, easing to 0 at
// `blend_distance` past where it stops being level.
float pull_of(const levelled_pad& levelled, const glm::vec3& position, const float blend_distance) {
    const ground_pad& pad = levelled.pad;
    const glm::vec3 offset = horizontal(position - pad.center);
    const glm::vec2 out(std::abs(glm::dot(offset, across_of(pad))) - pad.half_width - levelled.flat_outset,
                        std::abs(glm::dot(offset, pad.along)) - pad.half_length - levelled.flat_outset);
    const float outside = glm::length(glm::max(out, glm::vec2(0.0f)));
    if (blend_distance <= 0.0f) {
        return outside > 0.0f ? 0.0f : 1.0f;
    }
    const float t = clamp01(outside / blend_distance);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}
}

std::vector<glm::vec3> pad_footprint_points(const ground_pad& pad, const float spacing) {
    const float step = std::max(0.05f, spacing);
    const glm::vec3 across = across_of(pad);
    const int steps_across = std::max(1, static_cast<int>(std::ceil(2.0f * pad.half_width / step)));
    const int steps_along = std::max(1, static_cast<int>(std::ceil(2.0f * pad.half_length / step)));
    std::vector<glm::vec3> points;
    points.reserve(static_cast<std::size_t>((steps_across + 1) * (steps_along + 1)));
    for (int i = 0; i <= steps_across; ++i) {
        const float x = pad.half_width * (2.0f * static_cast<float>(i) / static_cast<float>(steps_across) - 1.0f);
        for (int j = 0; j <= steps_along; ++j) {
            const float y = pad.half_length * (2.0f * static_cast<float>(j) / static_cast<float>(steps_along) - 1.0f);
            points.push_back(pad.center + across * x + pad.along * y);
        }
    }
    return points;
}

float levelled_height(const std::vector<levelled_pad>& pads, const glm::vec3& position, const float height,
                      const float blend_distance) {
    float weights = 0.0f;
    float weighted_level = 0.0f;
    float strongest = 0.0f;
    for (const levelled_pad& pad : pads) {
        const float pull = pull_of(pad, position, blend_distance);
        if (pull <= 0.0f) {
            continue;
        }
        if (pull >= 1.0f) {
            return pad.level;
        }
        // Unbounded towards a pad's flat, so its level alone counts there.
        const float weight = pull / (1.0f - pull);
        weights += weight;
        weighted_level += weight * pad.level;
        strongest = std::max(strongest, pull);
    }
    if (weights <= 0.0f) {
        return height;
    }
    return height + (weighted_level / weights - height) * strongest;
}

glm::vec2 levelled_reach(const levelled_pad& levelled, const float blend_distance) {
    const ground_pad& pad = levelled.pad;
    const float extra = levelled.flat_outset + std::max(0.0f, blend_distance);
    const float across = pad.half_width + extra;
    const float along = pad.half_length + extra;
    const glm::vec3 side = across_of(pad);
    return glm::vec2(std::abs(side.x) * across + std::abs(pad.along.x) * along,
                     std::abs(side.z) * across + std::abs(pad.along.z) * along);
}
