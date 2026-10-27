#include "game/shot_setup.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/trigonometric.hpp>

namespace {
// Steps made by `held` seconds of holding, the press included.
float repeats_by(const float held, const setup_tuning& tuning) {
    if (held < 0.0f) {
        return 0.0f;
    }
    const float repeating = held - tuning.repeat_delay_seconds;
    if (repeating < 0.0f) {
        return 1.0f;
    }
    // The rate rises linearly to its top, then holds there.
    const float rising = tuning.repeat_acceleration > 0.0f
        ? std::max(0.0f, (tuning.max_repeat_rate - tuning.repeat_rate) / tuning.repeat_acceleration)
        : 0.0f;
    const float t = std::min(repeating, rising);
    const float steps = tuning.repeat_rate * t + 0.5f * tuning.repeat_acceleration * t * t +
        tuning.max_repeat_rate * std::max(0.0f, repeating - rising);
    return 1.0f + std::floor(steps);
}
}

float hand_sign(const handedness hand) {
    return hand == handedness::left ? -1.0f : 1.0f;
}

setup_limits setup_limits_for(const club_definition& club, const setup_tuning& tuning) {
    const bool putter = club.club_class == "putter";
    setup_limits limits;
    limits.face_open_min = -glm::radians(club.spec.max_face_closed_degrees);
    limits.face_open_max = glm::radians(club.spec.max_face_open_degrees);
    limits.ball_position_min = -1.0f;
    limits.ball_position_max = 1.0f;
    const int steps = putter ? tuning.putter_backswing_steps : tuning.backswing_steps;
    limits.backswing_step = 1.0f / static_cast<float>(std::max(1, steps));
    limits.backswing_max = putter ? 1.0f : tuning.overswing_max;
    return limits;
}

shot_setup stock_setup(const club_definition& club, const ball_lie lie, const glm::vec3& ball, const float aim,
                       const handedness hand, const float face_lean, const setup_tuning& tuning) {
    shot_setup setup;
    setup.ball_start = ball;
    setup.club_id = club.id;
    setup.aim_angle = wrap_angle(aim);
    setup.face_open = (lie == ball_lie::bunker ? glm::radians(club.spec.sand_face_open_degrees) : 0.0f) + face_lean;
    setup.ball_position = club.spec.stock_ball_position;
    setup.backswing = club.spec.stock_backswing;
    setup.hand = hand;
    return clamped_setup(setup, setup_limits_for(club, tuning));
}

shot_setup clamped_setup(const shot_setup& setup, const setup_limits& limits) {
    shot_setup clamped = setup;
    const auto finite = [](const float value) { return std::isfinite(value) ? value : 0.0f; };
    clamped.aim_angle = wrap_angle(finite(setup.aim_angle));
    clamped.face_open = std::clamp(finite(setup.face_open), limits.face_open_min, limits.face_open_max);
    clamped.ball_position = std::clamp(finite(setup.ball_position), limits.ball_position_min, limits.ball_position_max);
    clamped.backswing = std::clamp(finite(setup.backswing), limits.backswing_step, limits.backswing_max);
    return clamped;
}

shot_setup stepped_setup(const shot_setup& setup, const setup_steps& steps, const setup_limits& limits,
                         const setup_tuning& tuning) {
    shot_setup stepped = setup;
    stepped.aim_angle = setup.aim_angle + glm::radians(tuning.aim_step_degrees) * static_cast<float>(steps.aim) +
        steps.aim_radians;
    stepped.face_open = setup.face_open + glm::radians(tuning.face_step_degrees) * static_cast<float>(steps.face);
    stepped.ball_position = setup.ball_position + tuning.ball_position_step * static_cast<float>(steps.ball);
    stepped.backswing = setup.backswing + limits.backswing_step * static_cast<float>(steps.backswing);
    return clamped_setup(stepped, limits);
}

float world_face_yaw(const shot_setup& setup) {
    return setup.aim_angle - hand_sign(setup.hand) * setup.face_open;
}

shot_setup aimed_at(const shot_setup& setup, const glm::vec3& target) {
    shot_setup aimed = setup;
    if (horizontal_distance(setup.ball_start, target) > 0.0f) {
        aimed.aim_angle = yaw_towards(setup.ball_start, target);
    }
    return aimed;
}

int dial_steps(const float held_before, const float held_after, const setup_tuning& tuning) {
    if (held_after < 0.0f) {
        return 0;
    }
    return static_cast<int>(repeats_by(held_after, tuning) - repeats_by(held_before, tuning));
}
