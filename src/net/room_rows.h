#pragma once

// My room's rows as the game reads them (game_state::online): its members
// with their names, golfers, motion, balls, carts, loadouts and bags, its
// groups and greens' flags, and the shots played and items used in it. net_client hands every row of the room subscription here.
// Also the estimate of the server's clock, taken from my own avatar_motion
// rows: the server stamps them when it accepts my motion.

#include "game/game_state.h"
#include "net/stdb_bridge.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

// The server's clock from this client's: server time minus local time. Each
// of my avatar_motion rows arrives after the server stamped it, so every
// sample is at most the true offset; the largest is the closest.
class server_clock {
public:
    void sample(std::int64_t server_time, std::int64_t local_now);
    // 0 until the first sample.
    std::int64_t now(std::int64_t local_now) const;

private:
    std::optional<std::int64_t> offset_;
};

class room_rows {
public:
    // A row of my room, or a player row (their names). False for rows of
    // other tables.
    bool apply(const stdb_event& event, std::uint64_t my_account, std::int64_t local_now, server_clock& clock,
               game_state& state);
    // Writes what changed into state.online (players, balls, carts, loadouts, bags, flags, group sizes).
    void publish(game_state& state);
    // Out of the room: none of its rows apply any more.
    void clear(game_state& state);

private:
    struct member {
        std::uint64_t group_id = 0;
        std::vector<int> round_strokes;
    };
    struct motion {
        net_motion value;
        std::int64_t at = 0;
    };
    struct named {
        std::string name;
        handedness hand = handedness::right;
        std::uint8_t natural_shape = natural_shape_unset;
    };

    std::map<std::uint64_t, member> members_;
    std::map<std::uint64_t, named> names_;
    std::map<std::uint64_t, motion> motions_;
    std::map<std::uint64_t, room_ball> balls_;
    std::map<std::uint64_t, room_cart> carts_;
    std::map<std::uint64_t, loadout> loadouts_;
    std::map<std::uint64_t, room_bag> bags_;
    std::map<std::uint64_t, int> flags_;  // green_flag rows by id: their hole
    std::map<std::uint64_t, int> group_sizes_;
    bool changed_ = false;
};
