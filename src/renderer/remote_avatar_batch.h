#pragma once

// Other players in the world marker batch: a chunky low-poly figure on foot,
// the golf cart (cart_batch.h, placed where they sit instead of at the
// camera) when driving, and their balls. Each player has a shirt tint; a
// group shares the colour of its members' caps and the ring at their feet.
// GL-free.

#include "renderer/world_marker_batch.h"

#include <cstddef>
#include <cstdint>

#include <glm/vec3.hpp>

struct render_remote_avatar {
    glm::vec3 position{0.0f};  // feet, on the ground
    float yaw = 0.0f;          // see yaw_direction in physics/vector_math.h
    bool in_cart = false;
    std::uint64_t player_id = 0;  // picks the tint
    std::uint64_t group_id = 0;   // 0: none
};

struct render_remote_ball {
    glm::vec3 position{0.0f};
    std::uint64_t player_id = 0;
};

glm::vec3 remote_tint(std::uint64_t player_id);
glm::vec3 group_highlight(std::uint64_t group_id);

// `eye_height` is the walking eye height: the figure stands a little taller,
// and a cart sits where it would around a driver's eyes.
void append_remote_avatar(world_marker_batch& batch, const render_remote_avatar& avatar, float eye_height);
void append_remote_ball(world_marker_batch& batch, const render_remote_ball& ball, float radius);

// Vertices of a figure on foot without a group ring.
std::size_t remote_figure_vertex_count();
