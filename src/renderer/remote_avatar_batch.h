#pragma once

// Other players in the world marker batch: a chunky low-poly figure, on foot
// or sat at the wheel of the whole golf cart (append_outside_cart_model in
// cart_batch.h) when driving, and their balls. Each player
// has a shirt tint; a group shares the colour of its members' caps. While they address their ball, their club is drawn at
// it as mine is, their hands on its grip; their emote props are held at
// their mouth (emote_props.h). GL-free.

#include "renderer/emote_props.h"
#include "renderer/world_marker_batch.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include <glm/vec3.hpp>

// Their club while they address their ball, as append_swing_club draws it.
struct render_remote_swing {
    glm::vec3 ball_position{0.0f};  // the drawn ball's centre
    float aim_angle = 0.0f;
    float power = 0.0f;
};

struct render_remote_avatar {
    glm::vec3 position{0.0f};  // feet, on the ground
    float yaw = 0.0f;          // see yaw_direction in physics/vector_math.h
    bool in_cart = false;
    std::uint64_t player_id = 0;  // picks the tint
    std::uint64_t group_id = 0;   // 0: none
    // How far into each emote they are (none: not playing).
    std::optional<float> smoke_elapsed;
    std::optional<float> drink_elapsed;
    std::optional<render_remote_swing> swing;
};

struct render_remote_ball {
    glm::vec3 position{0.0f};  // the drawn ball's centre
    std::uint64_t player_id = 0;
};

glm::vec3 remote_tint(std::uint64_t player_id);
glm::vec3 group_highlight(std::uint64_t group_id);

// `eye_height` is the walking eye height: the figure stands a little taller,
// and a cart sits where it would around a driver's eyes. `ball_radius` is the
// ball's drawn radius, which places a swinging club.
void append_remote_avatar(world_marker_batch& batch, const render_remote_avatar& avatar, float eye_height, float ball_radius);
void append_remote_ball(world_marker_batch& batch, const render_remote_ball& ball, float radius);

// Where a figure stands to address a ball, its hands on the club's grip
// (swing_club_grip), and which way it faces: at the ball, side on to the aim.
struct figure_stance {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};
figure_stance figure_address_stance(const glm::vec3& ball_position, float ball_radius, float aim_angle);

// Their emote props, held at their figure's mouth; none when no emote plays.
std::optional<emote_holder> remote_emote_holder(const render_remote_avatar& avatar, float eye_height);

// Vertices of a figure on foot, and of one sat in a cart (the cart not
// counted).
std::size_t remote_figure_vertex_count();
std::size_t remote_driver_vertex_count();
