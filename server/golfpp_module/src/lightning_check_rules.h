#pragma once

// One lightning check of the server (the scheduled check_lightning in
// lib.cpp): which rooms are in a thunderstorm now, by the weather at their
// course and the shared clock, and who in them is struck, by the game's rule
// (game/lightning_rules.h) rolled from the server's secret so no client can
// predict it. A calm room is passed over before anyone in it is looked up.
// Plain C++ for the reducer and the native tests.

#include "game/lightning_rules.h"
#include "server_content.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A room with someone in it and the course it is on (nullptr: unknown, passed over).
struct lightning_room_ref {
    std::uint64_t room_id = 0;
    const server_course* course = nullptr;
};

struct room_lightning_hit {
    std::uint64_t room_id = 0;
    lightning_strike_hit hit;
};

// The strikes over the check `now_ms` falls in. `golfers_in` reads the
// golfers of a room, and is called only for a room whose thunder is above
// thunderstorm_thunder. `seed` is the server's secret-based dice for this run.
std::vector<room_lightning_hit> check_lightning_rooms(const std::vector<lightning_room_ref>& rooms,
                                                      const std::function<std::vector<lightning_golfer>(std::uint64_t)>& golfers_in,
                                                      const server_content& content, std::int64_t now_ms, std::uint64_t seed);

// The strike a god command (god_strike) calls down on `account` where they
// stand: the same hit a check's roll gives, so one application serves both.
room_lightning_hit god_strike_hit(std::uint64_t room_id, std::uint64_t account, const glm::vec3& position, bool knock_out);

// The error id a god command is refused with: empty when the server allows
// god commands (server_config.allow_god, for a developer's local server only).
std::string god_command_refusal(bool allow_god);
