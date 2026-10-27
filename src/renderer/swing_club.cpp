#include "renderer/swing_club.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
// From the grip down the shaft to the head, from the shaft's line out to
// the face's middle, and the head's heel past the shaft's line, as the club
// models are drawn.
constexpr float club_shaft_length = 1.10f;
constexpr float club_face_from_shaft = 0.10f;
constexpr float club_heel_past_shaft = 0.03f;
constexpr float club_face_ahead_of_shaft = 0.026f;
// The shaft's lean from straight up towards the golfer at address.
constexpr float club_address_angle_degrees = 12.0f;
// Half the head's height: at address its bottom rests on the ground under the ball.
constexpr float club_head_half_height = 0.045f;
// The arms swing about the chest, this far up the shaft's line from the
// hands; at a full swing they turn arm_degrees and the wrists hinge the
// club hinge_degrees more. A putt rocks the shoulders only: at its longest
// backswing the head goes back about half a metre.
constexpr float chest_from_grip = 0.55f;
constexpr float arm_degrees = 100.0f;
constexpr float hinge_degrees = 90.0f;
constexpr float putt_arm_degrees = 18.0f;

struct club_address {
    glm::mat4 frame{1.0f};          // the club's turn at address, about its grip
    glm::vec3 grip{0.0f};
    glm::vec3 swing_axis{0.0f};     // turning about it takes the head back from the target, then up
    glm::vec3 chest{0.0f};
};

club_address address_of(const render_club_pose& pose, const float ball_visual_radius_meters) {
    const glm::vec3 forward = yaw_direction(pose.aim_angle);
    const glm::vec3 golfer = yaw_left(forward) * (pose.left_handed ? -1.0f : 1.0f);
    const float lean = glm::radians(club_address_angle_degrees);
    const glm::vec3 shaft = glm::normalize(world_up * std::cos(lean) + golfer * std::sin(lean) +
                                           forward * std::tan(pose.shaft_lean));
    // Square to the path, then turned about the shaft as the face is opened.
    const glm::mat4 square = posed_model(glm::vec3(0.0f), shaft, forward, glm::vec3(pose.left_handed ? -1.0f : 1.0f, 1.0f, 1.0f));
    const glm::mat4 turned = glm::rotate(glm::mat4(1.0f), pose.face_turn, shaft) * square;
    const glm::vec3 face_normal = glm::normalize(glm::vec3(turned * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
    const float radius = std::max(0.02f, ball_visual_radius_meters);
    glm::vec3 face_at = pose.ball_position - horizontal(face_normal) * radius;
    face_at.y = pose.ball_position.y - radius + club_head_half_height;

    club_address out;
    const glm::vec3 heel = glm::normalize(glm::vec3(turned * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
    out.grip = face_at + heel * club_face_from_shaft + shaft * club_shaft_length;
    // Its heel soled on the ground, the toe lifting as the shaft leans.
    out.grip.y = face_at.y + shaft.y * club_shaft_length + club_heel_past_shaft * std::sin(lean);
    // Moved back along the ground until the face's plane just touches the
    // ball, however it is turned and leant: the head is never in the ball.
    const glm::vec3 face_middle =
        out.grip - heel * club_face_from_shaft - shaft * club_shaft_length + face_normal * club_face_ahead_of_shaft;
    const glm::vec3 level = safe_normalize(horizontal(face_normal), forward);
    const float gap = glm::dot(pose.ball_position - face_middle, face_normal) - radius;
    out.grip += level * (gap / std::max(0.1f, glm::dot(level, face_normal)));
    out.frame = turned;
    out.swing_axis = glm::normalize(glm::cross(shaft, forward));
    out.chest = out.grip + shaft * chest_from_grip;
    return out;
}

glm::mat4 turned_about(const glm::vec3& point, const glm::vec3& axis, const float radians) {
    return glm::translate(glm::mat4(1.0f), point) * glm::rotate(glm::mat4(1.0f), radians, axis) *
        glm::translate(glm::mat4(1.0f), -point);
}

// The arms' turn about the chest, then the wrists' about the hands.
glm::mat4 swung(const club_address& address, const float raise, const bool putt) {
    const glm::mat4 arms =
        turned_about(address.chest, address.swing_axis, glm::radians(putt ? putt_arm_degrees : arm_degrees) * raise);
    if (putt) {
        return arms;
    }
    const glm::vec3 hands = glm::vec3(arms * glm::vec4(address.grip, 1.0f));
    return turned_about(hands, address.swing_axis, glm::radians(hinge_degrees) * raise) * arms;
}
}

glm::vec3 swing_club_grip(const render_club_pose& pose, const float ball_visual_radius_meters) {
    const club_address address = address_of(pose, ball_visual_radius_meters);
    return glm::vec3(swung(address, pose.raise, pose.putt) * glm::vec4(address.grip, 1.0f));
}

glm::mat4 swing_club_frame(const render_club_pose& pose, const float ball_visual_radius_meters, const prop_model& club) {
    const club_address address = address_of(pose, ball_visual_radius_meters);
    return swung(address, pose.raise, pose.putt) * glm::translate(glm::mat4(1.0f), address.grip) * address.frame *
        glm::translate(glm::mat4(1.0f), -club.point("grip"));
}

void append_swing_club(prop_batch& batch, const render_club_pose& pose, const float ball_visual_radius_meters,
                       const prop_model& club, const glow_floors& glow) {
    append_prop_model(batch, club, swing_club_frame(pose, ball_visual_radius_meters, club), glow);
}
