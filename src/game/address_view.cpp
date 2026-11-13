#include "game/address_view.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Towards my side of the line from the ball.
glm::vec3 stance_side(const shot_setup& setup) {
    return yaw_left(yaw_direction(setup.aim_angle)) * hand_sign(setup.hand);
}

// How far forward of the ball the stance's middle stands.
float stance_ahead(const shot_setup& setup, const game_tuning& tuning) {
    return -setup.ball_position * tuning.setup.ball_position_meters / tuning.scale.meters_per_world_unit;
}

glm::vec3 on_ground(const play_area& area, glm::vec3 point, frame_profile* profile = nullptr) {
    point.y = terrain_height(area, point, profile);
    return point;
}
}

address_look target_look(const camera_tuning& tuning) {
    return address_look{glm::radians(tuning.address_target_yaw_degrees), glm::radians(tuning.address_target_pitch_degrees)};
}

address_look down_look(const camera_tuning& tuning) {
    return address_look{glm::radians(tuning.address_down_yaw_degrees), glm::radians(tuning.address_down_pitch_degrees)};
}

address_look clamped_look(const address_look& look, const camera_tuning& tuning) {
    return address_look{
        std::clamp(look.yaw, glm::radians(tuning.address_min_yaw_degrees), glm::radians(tuning.address_max_yaw_degrees)),
        std::clamp(look.pitch, glm::radians(tuning.address_min_pitch_degrees), glm::radians(tuning.address_max_pitch_degrees))};
}

bool looking_down(const address_look& look, const camera_tuning& tuning) {
    const auto apart = [&look](const address_look& framed) {
        return std::abs(look.yaw - framed.yaw) + std::abs(look.pitch - framed.pitch);
    };
    return apart(down_look(tuning)) < apart(target_look(tuning));
}

address_look snap_destination(const address_look& look, const camera_tuning& tuning) {
    return looking_down(look, tuning) ? target_look(tuning) : down_look(tuning);
}

address_look snap_look_at(const look_snap& snap, const camera_tuning& tuning) {
    const float t = tuning.address_snap_seconds > 0.0f ? clamp01(snap.elapsed / tuning.address_snap_seconds) : 1.0f;
    const float eased = t * t * (3.0f - 2.0f * t);
    return address_look{snap.from.yaw + (snap.to.yaw - snap.from.yaw) * eased,
                        snap.from.pitch + (snap.to.pitch - snap.from.pitch) * eased};
}

bool snap_done(const look_snap& snap, const camera_tuning& tuning) {
    return snap.elapsed >= tuning.address_snap_seconds;
}

glm::vec3 address_stance_position(const play_area& area, const shot_setup& setup, const game_tuning& tuning,
                                  frame_profile* profile) {
    const float out = tuning.lies.stance_ball_distance_meters / tuning.scale.meters_per_world_unit;
    const glm::vec3 middle =
        setup.ball_start + stance_side(setup) * out + yaw_direction(setup.aim_angle) * stance_ahead(setup, tuning);
    return on_ground(area, middle, profile);
}

float address_facing_yaw(const shot_setup& setup) {
    return wrap_angle(setup.aim_angle - hand_sign(setup.hand) * glm::half_pi<float>());
}

address_stance stance_at_address(const play_area& area, const shot_setup& setup, const game_tuning& tuning,
                                 frame_profile* profile) {
    return address_stance{address_stance_position(area, setup, tuning, profile), address_facing_yaw(setup)};
}

float address_view_yaw(const shot_setup& setup, const address_look& look) {
    return wrap_angle(address_facing_yaw(setup) + hand_sign(setup.hand) * look.yaw);
}

eye_view address_eye(const play_area& area, const shot_setup& setup, const address_look& look,
                     const game_tuning& tuning) {
    const camera_tuning& camera = tuning.camera;
    const glm::vec3 stance = address_stance_position(area, setup, tuning);
    glm::vec3 eye = setup.ball_start + stance_side(setup) * camera.address_eye_out +
        yaw_direction(setup.aim_angle) * stance_ahead(setup, tuning);
    eye.y = stance.y + camera.address_eye_height;
    const glm::vec3 level = yaw_direction(address_view_yaw(setup, look));
    return eye_view{eye, glm::normalize(level * std::cos(look.pitch) + world_up * std::sin(look.pitch))};
}

address_feet address_feet_for(const play_area& area, const shot_setup& setup, const game_tuning& tuning) {
    const glm::vec3 forward = yaw_direction(setup.aim_angle);
    const glm::vec3 stance = address_stance_position(area, setup, tuning);
    const float half_width = 0.5f * tuning.lies.stance_width_meters / tuning.scale.meters_per_world_unit;
    return address_feet{setup.ball_start, on_ground(area, stance + forward * half_width),
                        on_ground(area, stance - forward * half_width)};
}
