#pragma once

// Other players in the world marker batch: a chunky low-poly figure, on foot
// or sat in a cart's seat (the cart is drawn on its own, cart_batch.h), its
// hands on the wheel when driving, and their balls. Each player
// has a shirt tint; a group shares the colour of its members' caps. While they address their ball, their club is drawn at
// it as mine is, their hands on its grip; what they smoke and drink is held
// at their mouth (held_props.h). GL-free.

#include "renderer/held_props.h"
#include "renderer/swing_club.h"
#include "renderer/world_marker_batch.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <glm/vec3.hpp>

struct render_remote_avatar {
    glm::vec3 position{0.0f};  // feet, on the ground
    float yaw = 0.0f;          // see yaw_direction in physics/vector_math.h
    bool seated = false;    // in a cart's seat, `position` under it
    bool at_wheel = false;  // seated in the driver's seat
    std::uint64_t player_id = 0;  // picks the tint
    std::uint64_t group_id = 0;   // 0: none
    // What they smoke and drink while they use it.
    std::optional<render_item_use> smoking;
    std::optional<render_item_use> drinking;
    // Addressing their ball: the club in their hands, as append_swing_club draws it.
    std::optional<render_club_pose> swing;
    // Their bag's model while it is on their back, and the club in their
    // hand (bag_batch.h); empty for none.
    std::string bag_model;
    std::string carried_club_model;
    bool left_handed = false;
};

struct render_remote_ball {
    glm::vec3 position{0.0f};  // the drawn ball's centre
    std::uint64_t player_id = 0;
};

glm::vec3 remote_tint(const figure_colors& colors, std::uint64_t player_id);
glm::vec3 group_highlight(const figure_colors& colors, std::uint64_t group_id);

// `eye_height` is the walking eye height: the figure stands a little taller,
// and sits on a cart's seat where my eyes are at its wheel. `ball_radius` is the
// ball's drawn radius, which places a swinging club.
void append_remote_avatar(world_marker_batch& batch, const render_remote_avatar& avatar, float eye_height, float ball_radius,
                          const scene_colors& colors);
// `glow` is the ball's glow floor (glow_floors::ball).
void append_remote_ball(world_marker_batch& batch, const render_remote_ball& ball, float radius, const figure_colors& colors,
                        float glow);

// What they smoke and drink, held at their figure's mouth, its models from
// `props`; none when they use nothing.
std::optional<prop_holder> remote_prop_holder(const render_remote_avatar& avatar, float eye_height, const prop_catalog& props);

// Vertices of a figure on foot, and of one sat in a cart.
std::size_t remote_figure_vertex_count();
std::size_t remote_seated_vertex_count();
