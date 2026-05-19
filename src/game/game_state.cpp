#include "game/game_state.h"

#include "core/input.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/terrain.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;

glm::vec3 aim_direction(const float aim_angle) {
    return glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
}

float radians(const float degrees) {
    return degrees * pi / 180.0f;
}

glm::vec3 address_camera_position(const game_state& state) {
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    return state.ball.position + left * 2.4f - forward * 0.7f + glm::vec3(0.0f, 2.0f, 0.0f);
}

float terrain_height_at(const game_tuning& tuning, const glm::vec3& position) {
    return sample_terrain_mesh(tuning.terrain_mesh_data, position, tuning.ground_y).point.y;
}

terrain_sample terrain_sample_at(const game_tuning& tuning, const glm::vec3& position) {
    return sample_terrain_mesh(tuning.terrain_mesh_data, position, tuning.ground_y);
}

glm::vec3 terrain_clamped_position(const game_tuning& tuning, glm::vec3 position) {
    position.y = terrain_height_at(tuning, position);
    return position;
}

glm::vec3 address_player_position(const game_state& state) {
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    glm::vec3 position = state.ball.position + left * state.tuning.player_stand_off_distance - forward * 0.4f;
    position.y = terrain_height_at(state.tuning, position);
    return position;
}

void select_previous_club(game_state& state) {
    if (state.tuning.clubs.empty()) {
        return;
    }

    if (state.selected_club == 0) {
        state.selected_club = state.tuning.clubs.size() - 1;
    } else {
        --state.selected_club;
    }
}

void select_next_club(game_state& state) {
    if (state.tuning.clubs.empty()) {
        return;
    }

    state.selected_club = (state.selected_club + 1) % state.tuning.clubs.size();
}

void launch_ball(game_state& state) {
    if (state.tuning.clubs.empty()) {
        return;
    }

    const club_stats& club = state.tuning.clubs[state.selected_club].stats;
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const float loft = radians(club.loft_degrees);
    const glm::vec3 launch_dir = glm::normalize(forward * std::cos(loft) + glm::vec3(0.0f, std::sin(loft), 0.0f));
    const float power = std::max(state.tuning.min_swing_power, state.swing.power);
    const float speed = club.power * power;

    state.shot_camera_position = address_camera_position(state);
    state.ball.velocity = launch_dir * speed;
    state.ball.spin = glm::vec3(-club.spin_bias * speed, 0.0f, club.accuracy * state.tuning.launch_side_spin_scale);
    state.swing = swing_state{};
    state.mode = game_mode::following_shot;
    ++state.stroke_count;
}

void place_player_near_ball(game_state& state) {
    const glm::vec3 forward = aim_direction(state.aim_angle);
    state.player.position = state.ball.position - forward * state.tuning.player_stand_off_distance;
    state.player.position.y = terrain_height_at(state.tuning, state.player.position);
    state.player.yaw = state.aim_angle;
}

void apply_ground_roll_friction(game_state& state, const float dt) {
    const terrain_sample terrain = terrain_sample_at(state.tuning, state.ball.position);
    if (state.ball.position.y > terrain.point.y + 0.001f) {
        return;
    }

    const glm::vec3 normal = glm::length(terrain.normal) > 0.00001f
        ? glm::normalize(terrain.normal)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    const float normal_speed = glm::dot(state.ball.velocity, normal);
    if (normal_speed > state.tuning.ground_settle_speed) {
        return;
    }

    state.ball.position.y = terrain.point.y;
    const glm::vec3 tangent_velocity = state.ball.velocity - normal * normal_speed;
    const float speed = glm::length(tangent_velocity);
    if (speed <= 0.0f) {
        state.ball.velocity = glm::vec3(0.0f);
        return;
    }

    const float speed_after_friction = std::max(0.0f, speed - state.tuning.ground_roll_friction * dt);
    state.ball.velocity = tangent_velocity * (speed_after_friction / speed);
}

void update_swing(game_state& state, const input_state& input, const float dt) {
    if (state.swing.phase == swing_phase::timing) {
        state.swing.elapsed += dt;
        state.swing.power = sample_swing_power(state.swing.elapsed);
    }

    if (!input.space.pressed) {
        return;
    }

    if (state.swing.phase == swing_phase::idle) {
        state.swing.phase = swing_phase::timing;
        state.swing.elapsed = 0.0f;
        state.swing.power = 0.0f;
        return;
    }

    launch_ball(state);
}

void update_walking(game_state& state, const input_state& input, const float dt) {
    if (input.left.is_down) {
        state.player.yaw += state.tuning.player_turn_rate * dt;
    }

    if (input.right.is_down) {
        state.player.yaw -= state.tuning.player_turn_rate * dt;
    }

    const glm::vec3 forward = aim_direction(state.player.yaw);
    if (input.up.is_down) {
        state.player.position += forward * state.tuning.player_walk_speed * dt;
    }

    if (input.down.is_down) {
        state.player.position -= forward * state.tuning.player_walk_speed * dt;
    }

    state.player.position.y = terrain_height_at(state.tuning, state.player.position);

    if (input.space.pressed && can_interact_with_ball(state)) {
        state.mode = game_mode::aiming;
        state.aim_angle = state.player.yaw;
        state.swing = swing_state{};
    }
}

void update_aiming(game_state& state, const input_state& input, const float dt) {
    if (input.left.is_down) {
        state.aim_angle += state.tuning.aim_turn_rate * dt;
    }

    if (input.right.is_down) {
        state.aim_angle -= state.tuning.aim_turn_rate * dt;
    }

    state.player.yaw = state.aim_angle;

    if (input.up.pressed) {
        select_previous_club(state);
    }

    if (input.down.pressed) {
        select_next_club(state);
    }

    if (input.space.pressed) {
        state.player.position = address_player_position(state);
        state.mode = game_mode::addressing;
        state.swing = swing_state{};
    }
}

void update_addressing(game_state& state, const input_state& input, const float dt) {
    if (input.up.pressed) {
        select_previous_club(state);
    }

    if (input.down.pressed) {
        select_next_club(state);
    }

    update_swing(state, input, dt);
}

void step_ball(game_state& state, const float dt) {
    if (!ball_is_moving(state.ball, state.tuning)) {
        const terrain_sample terrain = terrain_sample_at(state.tuning, state.ball.position);
        state.ball = resolve_terrain_collision(state.ball,
                                               terrain,
                                               state.tuning.ground_restitution,
                                               state.tuning.ground_friction);
        state.ball.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
        state.ball.spin = glm::vec3(0.0f, 0.0f, 0.0f);
        return;
    }

    const wind_state wind = sample_wind(state.tuning.wind_seed, state.hole_time, state.tuning.wind);
    state.ball = step(state.ball, wind, dt, state.tuning.physics);
    const terrain_sample terrain = terrain_sample_at(state.tuning, state.ball.position);
    state.ball = resolve_terrain_collision(state.ball,
                                           terrain,
                                           state.tuning.ground_restitution,
                                           state.tuning.ground_friction);
    apply_ground_roll_friction(state, dt);
}
}

game_state make_initial_game_state() {
    game_state state;
    state.tuning = default_game_tuning();
    state.ball.position = terrain_clamped_position(state.tuning, state.tuning.course.tee_position);
    place_player_near_ball(state);
    return state;
}

void retee_ball(game_state& state) {
    state.ball.position = terrain_clamped_position(state.tuning, state.tuning.course.tee_position);
    state.ball.velocity = glm::vec3(0.0f);
    state.ball.spin = glm::vec3(0.0f);
    state.swing = swing_state{};
    state.mode = game_mode::walking;
    place_player_near_ball(state);
}

void update_game(game_state& state, const input_state& input, const float dt) {
    const float clamped_dt = std::max(0.0f, std::min(dt, 0.05f));
    state.hole_time += clamped_dt;

    if (input.retee.pressed) {
        retee_ball(state);
        return;
    }

    if (state.mode == game_mode::walking) {
        update_walking(state, input, clamped_dt);
    } else if (state.mode == game_mode::aiming) {
        update_aiming(state, input, clamped_dt);
    } else if (state.mode == game_mode::addressing) {
        update_addressing(state, input, clamped_dt);
    }

    if (state.mode == game_mode::following_shot || ball_is_moving(state.ball, state.tuning)) {
        state.mode = game_mode::following_shot;
        step_ball(state, clamped_dt);

        if (!ball_is_moving(state.ball, state.tuning)) {
            state.ball.velocity = glm::vec3(0.0f);
            state.ball.spin = glm::vec3(0.0f);
            state.mode = game_mode::walking;
        }
    }
}

bool ball_is_moving(const ball_state& ball, const game_tuning& tuning) {
    const float terrain_height = terrain_height_at(tuning, ball.position);
    return glm::length(ball.velocity) > tuning.stop_speed || ball.position.y > terrain_height + 0.001f;
}

bool ball_is_moving(const ball_state& ball) {
    return ball_is_moving(ball, default_game_tuning());
}

bool can_interact_with_ball(const game_state& state) {
    const glm::vec3 delta = state.player.position - state.ball.position;
    const glm::vec3 horizontal_delta(delta.x, 0.0f, delta.z);
    return glm::length(horizontal_delta) <= state.tuning.ball_interact_radius
        && !ball_is_moving(state.ball, state.tuning);
}
