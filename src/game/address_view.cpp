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

address_look snap_destination(const address_look& look, const camera_tuning& tuning) {
    const address_look target = target_look(tuning);
    const address_look down = down_look(tuning);
    const auto apart = [&look](const address_look& framed) {
        return std::abs(look.yaw - framed.yaw) + std::abs(look.pitch - framed.pitch);
    };
    return apart(target) <= apart(down) ? down : target;
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

address_marks address_marks_for(const play_area& area, const shot_setup& setup, const club_definition& club,
                                const game_tuning& tuning) {
    const setup_tuning& marks = tuning.setup;
    const float unit = tuning.scale.meters_per_world_unit;
    const glm::vec3 forward = yaw_direction(setup.aim_angle);
    const glm::vec3 side = stance_side(setup);
    address_marks out;
    out.ball = setup.ball_start;
    out.stance = address_stance_position(area, setup, tuning);
    const float half_width = 0.5f * tuning.lies.stance_width_meters / unit;
    out.lead_foot = on_ground(area, out.stance + forward * half_width);
    out.trail_foot = on_ground(area, out.stance - forward * half_width);

    // The face's leading edge runs across where it points, heel on my side.
    const glm::vec3 face = yaw_direction(world_face_yaw(setup));
    const glm::vec3 edge = yaw_left(face) * hand_sign(setup.hand);
    const glm::vec3 behind = setup.ball_start - face * (tuning.scale.ball_visual_radius_meters / unit);
    const float half = marks.face_line_half_length_meters / unit;
    out.face_heel = on_ground(area, behind + edge * half);
    out.face_toe = on_ground(area, behind - edge * half);

    const glm::vec3 toe_line = setup.ball_start + side * (marks.stick_out_meters / unit) + forward * stance_ahead(setup, tuning);
    const float stick = marks.stick_half_length_meters / unit;
    out.stick_back = on_ground(area, toe_line - forward * stick);
    out.stick_front = on_ground(area, toe_line + forward * stick);
    shot_setup stock = setup;
    stock.ball_position = club.spec.stock_ball_position;
    out.stock_tick = on_ground(area, toe_line - forward * stance_ahead(stock, tuning));
    return out;
}
