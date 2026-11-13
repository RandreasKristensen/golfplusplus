#pragma once
// Paired dap availability and clock boundaries, shared by client and server.
// Accepted positions only; no client pose or frame dt decides an award.
#include "game/net_types.h"
#include "game/game_tuning.h"
#include <string>
struct dap_participant {
    std::uint64_t account = 0;
    std::uint64_t room = 0;
    glm::vec3 position{0.0f};
    motion_mode mode = motion_mode::idle;
    bool busy = false;
};
std::string dap_award_key(std::uint64_t pair_id, std::uint64_t golfer_id);
bool dap_running(std::int64_t started_at, std::int64_t now, const dap_tuning& tuning);
std::string check_dap(const dap_participant& first, const dap_participant& second,
    const game_tuning& tuning);
