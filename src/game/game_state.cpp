#include "game/game_state.h"

#include "core/input.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
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

    const club_stats& club = state.tuning.clubs[state.selected_club];
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const float loft = radians(club.loft_degrees);
    const glm::vec3 launch_dir = glm::normalize(forward * std::cos(loft) + glm::vec3(0.0f, std::sin(loft), 0.0f));
    const float power = std::max(state.tuning.min_swing_power, state.swing.power);
    const float speed = club.power * power;

    state.ball.velocity = launch_dir * speed;
    state.ball.spin = glm::vec3(-club.spin_bias * speed, 0.0f, club.accuracy * state.tuning.launch_side_spin_scale);
    state.swing = swing_state{};
    ++state.stroke_count;
}

void apply_ground_roll_friction(game_state& state, const float dt) {
    if (state.ball.position.y > state.tuning.ground_y + 0.001f) {
        return;
    }

    if (std::abs(state.ball.velocity.y) <= state.tuning.ground_settle_speed) {
        state.ball.position.y = state.tuning.ground_y;
        state.ball.velocity.y = 0.0f;
    }

    glm::vec3 horizontal(state.ball.velocity.x, 0.0f, state.ball.velocity.z);
    const float speed = glm::length(horizontal);
    if (speed <= 0.0f) {
        return;
    }

    const float speed_after_friction = std::max(0.0f, speed - state.tuning.ground_roll_friction * dt);
    const glm::vec3 damped = horizontal * (speed_after_friction / speed);
    state.ball.velocity.x = damped.x;
    state.ball.velocity.z = damped.z;
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

void step_ball(game_state& state, const float dt) {
    if (!ball_is_moving(state.ball, state.tuning)) {
        state.ball.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
        state.ball.spin = glm::vec3(0.0f, 0.0f, 0.0f);
        return;
    }

    const wind_state wind = sample_wind(state.tuning.wind_seed, state.hole_time, state.tuning.wind);
    state.ball = step(state.ball, wind, dt, state.tuning.physics);
    state.ball = resolve_ground_collision(state.ball,
                                          state.tuning.ground_y,
                                          state.tuning.ground_restitution,
                                          state.tuning.ground_friction);
    apply_ground_roll_friction(state, dt);
}
}

game_state make_initial_game_state() {
    game_state state;
    state.tuning = default_game_tuning();
    state.ball.position = state.tuning.course.tee_position;
    return state;
}

void retee_ball(game_state& state) {
    state.ball.position = state.tuning.course.tee_position;
    state.ball.velocity = glm::vec3(0.0f);
    state.ball.spin = glm::vec3(0.0f);
    state.swing = swing_state{};
}

void update_game(game_state& state, const input_state& input, const float dt) {
    const float clamped_dt = std::max(0.0f, std::min(dt, 0.05f));
    state.hole_time += clamped_dt;

    if (input.retee.pressed) {
        retee_ball(state);
        return;
    }

    const bool moving = ball_is_moving(state.ball, state.tuning);
    if (!moving) {
        if (input.left.is_down) {
            state.aim_angle -= state.tuning.aim_turn_rate * clamped_dt;
        }

        if (input.right.is_down) {
            state.aim_angle += state.tuning.aim_turn_rate * clamped_dt;
        }

        if (input.up.pressed) {
            select_previous_club(state);
        }

        if (input.down.pressed) {
            select_next_club(state);
        }

        update_swing(state, input, clamped_dt);
    }

    step_ball(state, clamped_dt);
}

bool ball_is_moving(const ball_state& ball, const game_tuning& tuning) {
    return glm::length(ball.velocity) > tuning.stop_speed || ball.position.y > 0.001f;
}

bool ball_is_moving(const ball_state& ball) {
    return ball_is_moving(ball, default_game_tuning());
}
