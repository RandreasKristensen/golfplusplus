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
constexpr float aim_turn_rate = 1.8f;
constexpr float launch_elevation = 0.34f;
constexpr float stop_speed = 0.08f;

glm::vec3 aim_direction(const float aim_angle) {
    return glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
}

void select_previous_club(game_state& state) {
    if (state.clubs.empty()) {
        return;
    }

    if (state.selected_club == 0) {
        state.selected_club = state.clubs.size() - 1;
    } else {
        --state.selected_club;
    }
}

void select_next_club(game_state& state) {
    if (state.clubs.empty()) {
        return;
    }

    state.selected_club = (state.selected_club + 1) % state.clubs.size();
}

void launch_ball(game_state& state) {
    if (state.clubs.empty()) {
        return;
    }

    const club_stats& club = state.clubs[state.selected_club];
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const glm::vec3 launch_dir = glm::normalize(forward + glm::vec3(0.0f, launch_elevation, 0.0f));
    const float power = std::max(0.15f, state.swing.power);
    const float speed = club.power * power;

    state.ball.velocity = launch_dir * speed;
    state.ball.spin = glm::vec3(-club.spin_bias * speed, 0.0f, club.accuracy * 4.0f);
    state.swing = swing_state{};
    ++state.stroke_count;
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
    if (!ball_is_moving(state.ball)) {
        state.ball.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
        state.ball.spin = glm::vec3(0.0f, 0.0f, 0.0f);
        return;
    }

    const wind_state wind = sample_wind(state.wind_seed, state.hole_time);
    state.ball = step(state.ball, wind, dt);
    state.ball = resolve_ground_collision(state.ball, 0.0f, 0.35f, 0.08f);
}
}

game_state make_initial_game_state() {
    game_state state;
    state.ball.position = glm::vec3(0.0f, 0.0f, 0.0f);
    state.clubs = {
        club_stats{18.0f, 0.85f, 0.15f},
        club_stats{28.0f, 0.70f, 0.08f},
        club_stats{10.0f, 0.95f, 0.03f}
    };
    return state;
}

void update_game(game_state& state, const input_state& input, const float dt) {
    const float clamped_dt = std::max(0.0f, std::min(dt, 0.05f));
    state.hole_time += clamped_dt;

    const bool moving = ball_is_moving(state.ball);
    if (!moving) {
        if (input.left.is_down) {
            state.aim_angle -= aim_turn_rate * clamped_dt;
        }

        if (input.right.is_down) {
            state.aim_angle += aim_turn_rate * clamped_dt;
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

bool ball_is_moving(const ball_state& ball) {
    return glm::length(ball.velocity) > stop_speed || ball.position.y > 0.001f;
}
