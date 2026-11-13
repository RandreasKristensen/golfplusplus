#include "check_runtime.h"
#include "game/course_session.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/online_play.h"
#include "physics/vector_math.h"
#include <algorithm>
#include <cstdio>
#include <thread>

bool live_check::require(const char* label, const bool passed) {
    std::printf("%s  %s\n", passed ? "PASS" : "FAIL", label);
    if (!passed) {
        for (std::size_t i = 0; i < peers_.size(); ++i) {
            const auto& p = *peers_[i]; const auto& s = p.state;
            std::printf("  client %zu status=%d ready=%d pos=(%.2f,%.2f,%.2f) breaches=%llu failure=%s\n",
                i, static_cast<int>(p.client.status()), s.online.room_ready,
                s.player.position.x, s.player.position.y, s.player.position.z,
                static_cast<unsigned long long>(s.motion_sync.breaches), failure_id(p.client.failure()).c_str());
            for (const auto& f : p.failures) { std::printf("    %s: %s\n", f.reducer.c_str(), f.error.c_str()); }
            for (const auto& [id, player] : s.online.players) {
                std::printf("    player=%llu name=%s group=%llu scores=%zu\n", static_cast<unsigned long long>(id), player.name.c_str(), static_cast<unsigned long long>(player.group_id), player.round_strokes.size());
            }
            for (const auto& [id, size] : s.online.group_sizes) { std::printf("    group=%llu count=%d\n", static_cast<unsigned long long>(id), size); }
        }
    }
    return passed;
}
void live_check::tick() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - previous_).count(); previous_ = now;
    for (auto& peer : peers_) {
        auto& p = *peer; auto& s = p.state;
        p.client.update(s);
        while (auto f = p.client.take_reducer_failure()) { p.failures.push_back(*f); }
        p.failures.insert(p.failures.end(), s.online.refusals.begin(), s.online.refusals.end());
        s.online.refusals.clear();
        p.shots.insert(p.shots.end(), s.online.shots.begin(), s.online.shots.end());
        if (!s.online.room_ready || !s.online.room) { p.adopted_room = 0; continue; }
        if (p.adopted_room != s.online.room->room_id || !s.hub_flow.travel_course.empty()) {
            if (!s.hub_flow.travel_course.empty() && s.hub_flow.travel_course != s.online.room->course_id) { continue; }
            if (!enter_place(s, s.online.room->course_id)) { continue; }
            s.hub_flow = hub_flow_state{}; adopt_online_arrival(s);
            p.adopted_room = s.online.room->room_id;
        }
        update_game(s, p.input, dt);
    }
}
bool live_check::until(const std::function<bool()>& done, const float seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<float>(seconds);
    do { tick(); if (done()) { return true; } } while (std::chrono::steady_clock::now() < end);
    return false;
}
void live_check::wait(const float seconds) { until([] { return false; }, seconds); }
bool live_check::connect(const online_config& config) {
    for (int i = 0; i < 6; ++i) {
        auto client = net_client::create(config, "", "", content_.content_hash);
        if (!require("actual C++/Rust ABI agrees", client.has_value())) { return false; }
        auto s = make_game_state(content_, save_data{}); s.play = play_mode::online;
        peers_.push_back(std::make_unique<check_peer>(check_peer{std::move(*client), std::move(s), {}, {}, {}}));
        peers_.back()->client.begin_login();
    }
    if (!require("six anonymous protocol/content/account handshakes", until([&] {
        return std::all_of(peers_.begin(), peers_.end(), [](const auto& p) { return p->client.guest() && p->state.online.status == net_status::connected && p->state.online.account_id != 0; });
    }))) { return false; }
    for (auto& p : peers_) { p->client.claim_name("CHECK" + std::to_string(p->state.online.account_id)); }
    return require("six names accepted", until([&] {
        return std::all_of(peers_.begin(), peers_.end(), [](const auto& p) { return !p->state.online.display_name.empty(); });
    }));
}
bool live_check::walk(const std::size_t peer, const glm::vec3 target, const float seconds) {
    auto& p = (*this)[peer];
    const bool reached = until([&] {
        const auto delta = target - p.state.player.position;
        const bool close = glm::length(glm::vec2(delta.x, delta.z)) < 0.3f;
        p.input.forward_held = !close;
        p.input.look_yaw = close ? 0.0f : wrap_angle(yaw_towards(p.state.player.position, target) - p.state.player.yaw);
        return close;
    }, seconds);
    p.input = {}; send_motion_now(p.state); wait(0.3f);
    const auto me = my_room_player(p.state);
    return require("walk reaches target through accepted fixed steps", reached && me && me->motion && glm::length(me->motion->position - p.state.player.position) < 0.4f);
}
void live_check::send(const std::size_t peer, net_command command) {
    auto& s = state(peer); send_motion_now(s);
    command.room_id = s.online.room ? s.online.room->room_id : 0;
    s.net_commands.push_back(std::move(command));
}
void live_check::action(const std::size_t peer, const net_command_type type) {
    net_command c; c.type = type;
    c.reservation_id = state(peer).online.reservation ? state(peer).online.reservation->reservation_id : 0;
    send(peer, c);
}
bool live_check::refusal(const std::size_t peer, const std::string& reducer, const std::string& error) {
    auto& failures = (*this)[peer].failures;
    const bool found = until([&] { return std::any_of(failures.begin(), failures.end(), [&](const auto& f) { return f.reducer == reducer && f.error == error; }); });
    if (!require((reducer + " refuses " + error).c_str(), found)) { return false; }
    failures.erase(std::remove_if(failures.begin(), failures.end(), [&](const auto& f) { return f.reducer == reducer && f.error == error; }), failures.end());
    return true;
}
bool live_check::open(const std::size_t peer) {
    net_command c; c.type = net_command_type::open_tee_book; c.course_id = course; send(peer, c); tick();
    return require("scoped book acknowledgement and server clock", until([&] { const auto& o = state(peer).online; return o.book_ready && o.book_course_id == course && o.server_now > 0; }));
}
bool live_check::book(const std::size_t peer, const std::int64_t at) {
    net_command c; c.type = net_command_type::book_tee; c.course_id = course; c.scheduled_at = at;
    c.reservation_id = state(peer).online.reservation ? state(peer).online.reservation->reservation_id : 0; send(peer, c);
    return require("booking row acknowledges requested UTC time", until([&] { const auto& r = state(peer).online.reservation; return r && r->scheduled_at == at; }));
}
bool live_check::travel(const std::size_t peer, const std::string& destination) {
    (*this)[peer].input.interact = true; tick(); (*this)[peer].input = {};
    if (!require("F at physical bus opens transport choices", state(peer).hub_flow.screen == hub_screen::bus)) { return false; }
    request_bus_travel(state(peer), false);
    return require("bus adopts complete destination snapshot", until([&] {
        const auto& s = state(peer); return s.online.room_ready && s.online.room && s.online.room->course_id == destination && s.place.id == destination && s.hub_flow.travel_course.empty() && my_room_player(s) && my_room_player(s)->motion && s.online.bags.count(s.online.account_id) && s.online.loadouts.count(s.online.account_id);
    }));
}
