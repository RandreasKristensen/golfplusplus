#pragma once

// A client and an observer against the reducer's real movement and cart
// rules, over a deterministic faulty link. Snapshots carry a revision, as
// ordered SDK transactions do; outbound inputs may be lost, delayed or
// reordered.

#include "game/game_state.h"
#include "cart_rules.h"
#include "movement_rules.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sync_harness {
    game_state client;
    game_state observer;
    room_cart cart;
    net_motion avatar;  // my avatar row
    mover_state mover;  // my motion_state row
    std::optional<glm::vec3> ball;  // my ball row, at rest
    cart_pose parking;
    double now = 1.0;
    double accepted_at = 1.0;
    double next_id = 1.0;
    float latency = 0.04f;
    float jitter = 0.0f;
    float loss = 0.0f;
    bool connected = true;
    bool drop_next_motion = false;
    std::string refuse_next_motion;
    std::uint32_t random = 37;
    std::vector<std::string> refusals;

    sync_harness();
    void tick(const game_input& input = {}, float dt = 0.02f);
    void run(float seconds, const game_input& input = {});
    void enter();
    void exit();
    void disconnect();
    void reconnect();
    void publish();
    bool invariant() const;
    bool agrees() const;
    // The room as the server has it, for its movement rules.
    move_world world() const;

private:
    struct message {
        double due = 0.0;
        net_command command;
    };
    struct snapshot {
        double due = 0.0;
        std::uint64_t revision = 0;
        room_cart cart;
        net_motion avatar;
        std::string reducer;
        std::string error;
    };
    std::vector<message> outbound;
    std::vector<snapshot> inbound;
    std::uint64_t revision = 0;
    std::uint64_t received = 0;
    float roll();
    void send();
    void reduce(const net_command& command);
};
