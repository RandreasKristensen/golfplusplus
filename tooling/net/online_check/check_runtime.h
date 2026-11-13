#pragma once
// Real SDK clients driven at wall-clock speed. Only complete snapshots place
// a client; navigation uses the game's fixed walking inputs.
#include "game/game_content.h"
#include "game/game_state.h"
#include "net/net_client.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <glm/vec3.hpp>
struct check_peer {
    net_client client;
    game_state state;
    game_input input;
    std::vector<reducer_failure> failures;
    std::vector<room_shot> shots;
    std::uint64_t adopted_room = 0;
};
class live_check {
public:
    explicit live_check(game_content content) : content_(std::move(content)) {}
    bool connect(const online_config& config);
    bool require(const char* label, bool passed);
    bool until(const std::function<bool()>& done, float seconds = 15.0f);
    void tick();
    void wait(float seconds);
    bool walk(std::size_t peer, glm::vec3 target, float seconds = 45.0f);
    bool refusal(std::size_t peer, const std::string& reducer, const std::string& error);
    void send(std::size_t peer, net_command command);
    void action(std::size_t peer, net_command_type type);
    bool book(std::size_t peer, std::int64_t at);
    bool open(std::size_t peer);
    bool travel(std::size_t peer, const std::string& course);
    check_peer& operator[](std::size_t peer) { return *peers_.at(peer); }
    game_state& state(std::size_t peer) { return peers_.at(peer)->state; }
    std::string course;
    std::int64_t due = 0;
private:
    game_content content_;
    std::vector<std::unique_ptr<check_peer>> peers_;
    std::chrono::steady_clock::time_point previous_ = std::chrono::steady_clock::now();
};
bool check_booking(live_check& run);
bool check_social(live_check& run);
bool check_round(live_check& run);

bool check_practice(live_check& run);
