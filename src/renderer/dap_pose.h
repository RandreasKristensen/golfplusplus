#pragma once
// One procedural fist timeline for local first-person, remote figures and
// shadows. World geometry meets at the paired midpoint; the camera stays free.
#include "game/net_types.h"
#include "game/game_tuning.h"
#include "renderer/prop_batch.h"
#include <glm/vec3.hpp>
struct dap_hand_pose {
    glm::vec3 shoulder{0.0f};
    glm::vec3 hand{0.0f};
    glm::vec3 front{0.0f};
    float yaw = 0.0f;
    float radius = 0.0f;
    float arm_radius = 0.0f;
    bool left_handed = false;
    float thumb_share = 0.0f;
};
dap_hand_pose paired_dap_pose(const room_dap& pair, std::uint64_t account, handedness hand,
    std::int64_t now, const game_tuning& tuning);
void append_dap_hand(prop_batch& batch, const dap_hand_pose& pose, const glm::vec3& skin, const glm::vec3& shirt);
