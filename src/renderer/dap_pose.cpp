#include "renderer/dap_pose.h"
#include "renderer/camera_local.h"
#include "physics/vector_math.h"
#include <algorithm>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
dap_hand_pose paired_dap_pose(const room_dap& pair, const std::uint64_t account, const handedness hand,
    const std::int64_t now, const game_tuning& tuning) {
    const auto& t = tuning.dap;
    const float units = 1.0f / tuning.scale.meters_per_world_unit;
    const bool first = account == pair.first;
    const glm::vec3 feet = first ? pair.first_position : pair.second_position;
    const glm::vec3 other = first ? pair.second_position : pair.first_position;
    const glm::vec3 front = safe_normalize(glm::vec3(other.x - feet.x, 0.0f, other.z - feet.z), yaw_direction(0.0f));
    const glm::vec3 side = yaw_left(front) * (hand == handedness::left ? 1.0f : -1.0f);
    const float radius = t.fist_radius_meters * units;
    const glm::vec3 rest = feet + (world_up * t.rest_height_meters + side * t.hand_side_meters + front * t.hand_forward_meters) * units;
    // Fist surfaces touch at the same point, rather than overlap their centres.
    const glm::vec3 contact = (feet + other) * 0.5f + world_up * (t.hand_height_meters * units) - front * radius;
    const float phase = clamp01(server_seconds_between(pair.started_at, now) / t.seconds);
    const float amount = phase < t.contact_share ? phase / t.contact_share :
        phase <= t.release_share ? 1.0f : (1.0f - phase) / (1.0f - t.release_share);
    const float eased = amount * amount * (3.0f - 2.0f * amount);
    const glm::vec3 shoulder = feet + (world_up * t.hand_height_meters + side * t.hand_side_meters) * units;
    return {shoulder, glm::mix(rest, contact, eased), front, yaw_towards(feet, other), radius,
        t.arm_radius_meters * units, hand == handedness::left, t.thumb_share};
}
void append_dap_hand(prop_batch& batch, const dap_hand_pose& pose, const glm::vec3& skin, const glm::vec3& shirt) {
    batch.append(prop_mesh::cylinder, local_segment_model(glm::vec3(0.0f), glm::vec3(0,0,1),
        pose.shoulder, pose.hand, pose.arm_radius), shirt, 0.0f);
    batch.append(prop_mesh::sphere, posed_model(pose.hand, world_up, pose.front, glm::vec3(pose.radius)), skin, 0.0f);
    const glm::vec3 thumb = pose.hand + yaw_left(pose.front) * (pose.left_handed ? -pose.radius : pose.radius) * pose.thumb_share;
    batch.append(prop_mesh::sphere, posed_model(thumb, world_up, pose.front, glm::vec3(pose.radius * pose.thumb_share)), skin, 0.0f);
}
