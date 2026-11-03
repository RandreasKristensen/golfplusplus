#include "sync_harness.h"

#include "game/course_session.h"
#include "game/mode_dispatch.h"
#include "game/online_play.h"
#include "game/play_area.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"
#include "test_support.h"

#include <algorithm>
#include <utility>

namespace {
constexpr std::uint64_t me = 1;
}

sync_harness::sync_harness() : client(started_game(fixture_hub_course())), observer(client) {
    // A flat, empty patch makes the link the only source of uncertainty.
    client.area = play_area{};
    client.world.cart_roads.clear();
    cart = room_cart{glm::vec3(0.0f), 0.0f, 0};
    parking = cart_pose{cart.position, cart.yaw};
    client.cart.pose = parking;
    client.player.position = step_out_position(parking, cart_seat::driver, client.tuning.cart);
    avatar = current_motion(client);
    client.play = play_mode::online;
    client.online.status = net_status::connected;
    client.online.account_id = me;
    client.online.room_ready = true;
    observer.play = play_mode::online;
    observer.online.status = net_status::connected;
    observer.online.account_id = 2;
    observer.takes_cart = false;
    observer.area = client.area;
    mover = placed_mover(mover_state{}, avatar.position);
    mover.credit_seconds = client.tuning.server.max_motion_gap_seconds;
    mover.slack_left = client.tuning.server.motion_distance_slack;
    publish();
    run(0.2f);
}

float sync_harness::roll() {
    random = random * 1664525U + 1013904223U;
    return static_cast<float>(random >> 8) / 16777216.0f;
}

void sync_harness::publish() {
    inbound.push_back(snapshot{now + latency + roll() * jitter, ++revision, cart, avatar, {}, {}});
}

void sync_harness::send() {
    const std::vector<net_command> commands = std::move(client.net_commands);
    client.net_commands.clear();
    for (net_command command : commands) {
        if (!connected) {
            client.motion_sync.last_sent.reset();
            continue;
        }
        if (command.type == net_command_type::motion) {
            next_id += 0.001;
            command.motion.client_time = next_id;
            motion_sent(client, command, next_id);
            if (drop_next_motion) {
                drop_next_motion = false;
                continue;
            }
        }
        if (roll() >= loss) {
            outbound.push_back(message{now + latency + roll() * jitter, command});
        }
    }
}

void sync_harness::reduce(const net_command& command) {
    std::string error;
    const char* reducer = "";
    std::optional<cart_decision> decision;
    switch (command.type) {
    case net_command_type::motion: {
        reducer = reducer_update_motion;
        if (!refuse_next_motion.empty()) {
            error = std::move(refuse_next_motion);
            refuse_next_motion.clear();
            break;
        }
        const move_request request{command.motion, command.first_step, command.start, command.runs};
        const move_outcome moved = play_move(mover, cart, me, request, static_cast<float>(now - accepted_at), world());
        error = moved.error;
        if (error.empty() && !moved.repeated) {
            mover = moved.mover;
            avatar = moved.shown;
            // As update_motion keeps it: inside the club in hand's limits.
            const club_definition* club =
                client.selected_club < client.clubs.size() ? &client.clubs[client.selected_club] : nullptr;
            avatar.setup = at_ball(avatar.mode) && club != nullptr
                ? clamped_sent_setup(avatar.setup, *club, client.tuning.setup)
                : sent_setup{};
            if (moved.cart) { cart = *moved.cart; }
            accepted_at = now;
        }
        break;
    }
    case net_command_type::take_wheel:
        reducer = reducer_take_wheel;
        decision = take_wheel_rule(cart, me, mover.position, client.tuning);
        if (decision->cart) {
            mover.cart_velocity = glm::vec2(0.0f);
            mover.cart_yaw_rate = 0.0f;
            mover.cart_lift_seconds = 0.0f;
            mover.cart_vertical = vertical_body{};
        }
        break;
    case net_command_type::leave_wheel:
        reducer = reducer_leave_wheel;
        decision = leave_wheel_rule(cart, me);
        if (decision->cart) {
            const glm::vec3 out = step_out_position(cart_pose{cart.position, cart.yaw}, cart_seat::driver, client.tuning.cart);
            mover = placed_mover(mover, glm::vec3(out.x, terrain_height(client.area, out), out.z));
        }
        break;
    case net_command_type::right_cart:
        reducer = reducer_right_cart;
        decision = right_cart_rule(cart, mover.position, client.tuning);
        break;
    default: return;
    }
    if (decision) {
        error = decision->error;
        if (decision->cart) { cart = *decision->cart; }
    }
    if (!error.empty()) { refusals.push_back(error); }
    inbound.push_back(snapshot{now + latency + roll() * jitter, ++revision, cart, avatar, reducer, error});
}

// The room as the server has it: my cart and ball, nobody else, and the client's weather.
move_world sync_harness::world() const {
    move_world moving{client.area, client.world.cart_roads, client.tuning};
    moving.walk_speed = client.tuning.player.walk_speed;
    moving.spec = my_cart_spec(client);
    moving.wetness = client.weather.wetness;
    moving.limits = movement_limits_for(client.tuning, moving.spec, {}, 0);
    moving.own_ball = ball;
    moving.walker_obstacles = walker_obstacles({cart_pose{cart.position, cart.yaw}}, client.area.cups, client.flags_in,
                                               client.tuning);
    return moving;
}

void sync_harness::tick(const game_input& input, const float dt) {
    now += dt;
    for (auto it = outbound.begin(); it != outbound.end();) {
        if (it->due <= now) {
            const net_command command = it->command;
            it = outbound.erase(it);
            reduce(command);
        } else { ++it; }
    }
    for (auto it = inbound.begin(); it != inbound.end();) {
        if (it->due > now) { ++it; continue; }
        if (connected && it->revision > received) {
            received = it->revision;
            for (game_state* state : {&client, &observer}) {
                state->online.carts[me] = it->cart;
                room_player player;
                player.motion = it->avatar;
                player.motion_at = static_cast<std::int64_t>(now * 1000000.0);
                state->online.players[me] = player;
            }
        }
        // Refusals are reducer results, never discarded by snapshot coalescing.
        if (connected && !it->error.empty()) {
            client.online.refusals.push_back({it->reducer, it->error});
        }
        it = inbound.erase(it);
    }
    for (game_state* state : {&client, &observer}) {
        state->online.server_now = static_cast<std::int64_t>(now * 1000000.0);
    }
    update_game(client, input, dt);
    update_game(observer, {}, dt);
    observer.net_commands.clear();
    send();
}

void sync_harness::run(const float seconds, const game_input& input) {
    const int frames = static_cast<int>(seconds / 0.02f) + 1;
    for (int i = 0; i < frames; ++i) { tick(input); }
}

void sync_harness::enter() {
    stand_at_door(client, *shown_cart(client, 0), cart_seat::driver);
    game_input input;
    input.forward_held = true;
    for (int i = 0; i < 60 && on_foot(client); ++i) { tick(input); }
    run(0.8f);
}

void sync_harness::exit() {
    game_input input;
    input.interact = true;
    tick(input);
    run(0.8f);
}

void sync_harness::disconnect() {
    connected = false;
    outbound.clear();
    inbound.clear();
    client.online.carts.clear();
    client.online.players.clear();
    client.online.room_ready = false;
    client.online.status = net_status::connecting;
    reset_motion_sync(client);
    client.net_commands.clear();
}

void sync_harness::reconnect() {
    connected = true;
    client.online.status = net_status::connected;
    client.online.room_ready = true;
    client.ride.reset();
    client.player.position = avatar.position;
    // Joining again starts the room's rows afresh.
    mover = placed_mover(mover_state{}, avatar.position);
    mover.credit_seconds = client.tuning.server.max_motion_gap_seconds;
    mover.slack_left = client.tuning.server.motion_distance_slack;
    publish();
    run(0.5f);
}

bool sync_harness::invariant() const {
    if (!driving_cart(client)) {
        const auto shown = shown_cart(client, 0);
        const auto row = client.online.carts.find(me);
        if (shown && row != client.online.carts.end() &&
            (!near(shown->position, row->second.position, 0.0001f) || !near(shown->yaw, row->second.yaw, 0.0001f))) {
            return false;
        }
    }
    return client.motion_sync.history.empty() || client.motion_sync.clock - client.motion_sync.history.front().sent_at <=
        client.tuning.net.max_unacked_seconds;
}

bool sync_harness::agrees() const {
    const auto mine = shown_cart(client, 0);
    const auto theirs = shown_cart(observer, me);
    return mine && theirs && near(mine->position, cart.position, 0.04f) && near(theirs->position, cart.position, 0.04f) &&
        near(mine->yaw, cart.yaw, 0.02f) && near(theirs->yaw, cart.yaw, 0.02f);
}
