#pragma once

// A knocked-out golfer's body (game/fallen_body.h) as drawn: the golfer's
// figure lying on its back on the ground, head towards its yaw, feet at the
// other end, arms a little out, legs straight and slightly apart, its cap on
// its head. Every part lies inside the low box that blocks you (length,
// half-width and height below), resting on the ground under it. Pieces of the
// figure are instances in the world marker batch (the avatars part, like the
// other golfers). Its bag, when on its back, lies beside it (bag_batch.h). GL-free.

#include "game/gear_paint.h"
#include "renderer/world_marker_batch.h"

#include <array>
#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

struct render_lying_body {
    glm::vec3 position{0.0f};  // the middle of the body, on the ground
    float yaw = 0.0f;          // the way the head points (physics/vector_math.h yaw_direction)
    std::uint64_t account = 0;  // picks the shirt, as for a golfer
    // The box it lies in (game/fallen_body.h body_solid), in metres.
    float length = 0.0f;
    float half_width = 0.0f;
    float height = 0.0f;
    // The ground's height under its feet, middle and head.
    std::array<float, 3> ground{0.0f, 0.0f, 0.0f};
    // The bag lying beside it; empty model for none.
    std::string bag_model;
    gear_paint bag_paint;
};

void append_lying_body(world_marker_batch& batch, const render_lying_body& body, const scene_colors& colors);
