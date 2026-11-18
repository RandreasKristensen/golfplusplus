#include "check_runtime.h"
#include "game/cart_path.h"
#include "game/course_session.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/online_play.h"
#include "physics/vector_math.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/geometric.hpp>
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
        while (auto f = p.client.take_reducer_failure()) { p.failures.push_back(*f);}
        // Recorded here; update_game then reacts to them as the game would.
        p.failures.insert(p.failures.end(), s.online.refusals.begin(), s.online.refusals.end());
        p.shots.insert(p.shots.end(), s.online.shots.begin(), s.online.shots.end());
        if (!s.online.room_ready || !s.online.room) { s.online.refusals.clear(); p.adopted_room = 0; continue; }
        if (p.adopted_room != s.online.room->room_id || !s.hub_flow.travel_course.empty()) {
            if ((!s.hub_flow.travel_course.empty() && s.hub_flow.travel_course != s.online.room->course_id) ||
                !enter_place(s, s.online.room->course_id)) { s.online.refusals.clear(); continue; }
            s.hub_flow = hub_flow_state{}; adopt_online_arrival(s);
            p.adopted_room = s.online.room->room_id;
        }
        update_game(s, p.input, dt);
        s.online.refusals.clear();  // each is recorded once, whether or not this frame took it
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
namespace {
// In the hub a golfer follows the paths (past the book and round the bus), then
// walks straight on to the target; elsewhere the walk is a straight line.
std::vector<glm::vec3> walking_way(const game_state& s, const glm::vec3& target) {
    if (!s.hub) { return {target}; }
    const path_graph graph = graph_of(s.area.cart_paths);
    std::vector<std::size_t> order(graph.nodes.size());
    for (std::size_t i = 0; i < order.size(); ++i) { order[i] = i; }
    std::sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b) {
        return horizontal_distance(graph.nodes[a], target) < horizontal_distance(graph.nodes[b], target); });
    const std::size_t from = nearest_node(graph, s.player.position);
    for (const std::size_t to : order) {
        const auto route = route_along(graph, from, to);
        if (!route) { continue; }
        std::vector<glm::vec3> way;
        for (std::size_t i = 1; i < route->size(); ++i) {
            const glm::vec3& now = graph.nodes[(*route)[i]];
            const bool last = i + 1 == route->size();
            const bool turns = !last && glm::length(glm::cross(horizontal(now - graph.nodes[(*route)[i - 1]]),
                horizontal(graph.nodes[(*route)[i + 1]] - now))) > 0.01f;
            if (last || turns) { way.push_back(now); }
        }
        way.push_back(target);
        return way;
    }
    return {target};
}
}
bool live_check::walk(const std::size_t peer, const glm::vec3 target, const float seconds) {
    auto& p = (*this)[peer];
    bool reached = true;
    for (const glm::vec3& waypoint : walking_way(p.state, target)) {
        reached = reached && until([&] {
            const auto delta = waypoint - p.state.player.position;
            const bool close = glm::length(glm::vec2(delta.x, delta.z)) < 0.3f;
            p.input.forward_held = !close;
            p.input.look_yaw = close ? 0.0f : wrap_angle(yaw_towards(p.state.player.position, waypoint) - p.state.player.yaw);
            return close;
        }, seconds);
    }
    p.input = {}; send_motion_now(p.state);
    // The server's row for those steps, however long it takes to arrive.
    const bool agreed = until([&] {
        const auto me = my_room_player(p.state);
        return me && me->motion && glm::length(me->motion->position - p.state.player.position) < 0.4f;
    }, 5.0f);
    if (!agreed) {
        const auto me = my_room_player(p.state);
        const auto row = me && me->motion ? me->motion->position : glm::vec3(NAN);
        std::printf("  walk peer %zu target (%.2f,%.2f) reached=%d row (%.2f,%.2f,%.2f)\n", peer, target.x, target.z, reached, row.x, row.y, row.z);
    }
    return require("walk reaches target through accepted fixed steps", reached && agreed);
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
bool live_check::course_has_carts() const {
    const auto found = std::find_if(content_.courses.begin(), content_.courses.end(), [&](const auto& c) { return c.id == course; });
    return found != content_.courses.end() && found->carts;
}
bool live_check::book(const std::size_t peer, const std::int64_t at) { return book(peer, at, peer == 3 || peer == 4); }
bool live_check::book(const std::size_t peer, const std::int64_t at, const bool takes_cart) {
    net_command c; c.type = net_command_type::book_tee; c.course_id = course; c.scheduled_at = at; c.takes_cart = takes_cart;
    c.reservation_id = state(peer).online.reservation ? state(peer).online.reservation->reservation_id : 0; send(peer, c);
    // The row says what the server stored: the choice, a cart only where the course has them.
    const bool stored = takes_cart && course_has_carts();
    return require("booking row acknowledges requested UTC time and cart or carry", until([&] {
        const auto& r = state(peer).online.reservation; return r && r->scheduled_at == at && r->takes_cart == stored;
    }));
}
bool live_check::travel(const std::size_t peer, const std::string& destination) {
    (*this)[peer].input.interact = true; tick(); (*this)[peer].input = {};
    if (!require("F at physical bus boards with no menu", state(peer).hub_flow.screen == hub_screen::none && !state(peer).hub_flow.travel_course.empty())) { return false; }
    return require("bus adopts complete destination snapshot", until([&] {
        const auto& s = state(peer); return s.online.room_ready && s.online.room && s.online.room->course_id == destination && s.place.id == destination && s.hub_flow.travel_course.empty() && my_room_player(s) && my_room_player(s)->motion && s.online.bags.count(s.online.account_id) && s.online.loadouts.count(s.online.account_id);
    }));
}
