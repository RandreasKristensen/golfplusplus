#include "net/net_client.h"

#include <cmath>
#include <limits>

#include "game/mode_dispatch.h"
#include "game/hub_flow.h"
#include "game/motion_sync.h"
#include "game/net_types.h"
#include "net/bridge_rows.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {
stdb_string view(const std::string& text) {
    return stdb_string{text.data(), text.size()};
}

// The reducers net_commands call: the game answers their refusals.
bool gameplay_reducer(const std::string& reducer) {
    for (const char* name : {reducer_update_motion, reducer_take_shot, reducer_tee_up, reducer_claim_collectible,
                             reducer_dap, reducer_use_item, reducer_place_practice_ball, reducer_pick_up_ball, reducer_give_up_hole, "start_booked_round",
                             reducer_leave_group, reducer_take_wheel, reducer_leave_wheel, reducer_board_cart,
                             reducer_leave_cart, reducer_right_cart, reducer_set_flag,
                             reducer_place_bag, reducer_take_club, reducer_set_golfer}) {
        if (reducer == name) {
            return true;
        }
    }
    return false;
}

// The bridge was built from net/client_bridge/src/ffi.rs; its structs must
// be laid out as stdb_bridge.h says, field for field.
bool layouts_agree() {
    const std::vector<std::size_t> expected = stdb_header_layout();
    std::vector<std::size_t> bridge(expected.size());
    return stdb_layout(bridge.data(), bridge.size()) == expected.size() && bridge == expected;
}

std::uint32_t subscribe(stdb_client* client, const std::vector<std::string>& queries) {
    std::vector<stdb_string> views;
    for (const std::string& query : queries) {
        views.push_back(view(query));
    }
    return stdb_subscribe(client, views.data(), views.size());
}
}

std::vector<std::size_t> stdb_header_layout() {
    std::vector<std::size_t> out;
    const auto describe = [&out](const std::size_t size, std::initializer_list<std::size_t> offsets) {
        out.push_back(size);
        out.insert(out.end(), offsets);
    };
    describe(sizeof(stdb_string), {offsetof(stdb_string, data), offsetof(stdb_string, len)});
    describe(sizeof(stdb_config),
             {offsetof(stdb_config, server_uri), offsetof(stdb_config, database), offsetof(stdb_config, auth_issuer),
              offsetof(stdb_config, auth_client_id), offsetof(stdb_config, auth_scopes),
              offsetof(stdb_config, auth_authorization_endpoint), offsetof(stdb_config, auth_token_endpoint),
              offsetof(stdb_config, page_signed_in), offsetof(stdb_config, page_failed), offsetof(stdb_config, anonymous)});
    describe(sizeof(stdb_player), {offsetof(stdb_player, account_id), offsetof(stdb_player, display_name),
                                   offsetof(stdb_player, online), offsetof(stdb_player, hand),
                                   offsetof(stdb_player, natural_shape),
                                   offsetof(stdb_player, favourite_colour)});
    describe(sizeof(stdb_golfer_skill), {offsetof(stdb_golfer_skill, id), offsetof(stdb_golfer_skill, golfer_id),
                                         offsetof(stdb_golfer_skill, skill_id), offsetof(stdb_golfer_skill, xp)});
    describe(sizeof(stdb_golfer_progress),
             {offsetof(stdb_golfer_progress, golfer_id), offsetof(stdb_golfer_progress, account_id),
              offsetof(stdb_golfer_progress, scope_id), offsetof(stdb_golfer_progress, holes_completed)});
    describe(sizeof(stdb_completed_course), {offsetof(stdb_completed_course, id), offsetof(stdb_completed_course, golfer_id),
                                             offsetof(stdb_completed_course, course_id)});
    describe(sizeof(stdb_collected),
             {offsetof(stdb_collected, id), offsetof(stdb_collected, golfer_id), offsetof(stdb_collected, collectible_id),
              offsetof(stdb_collected, repeatable), offsetof(stdb_collected, claim_count),
              offsetof(stdb_collected, claimed_at_holes_completed)});
    describe(sizeof(stdb_world_flag),
             {offsetof(stdb_world_flag, id), offsetof(stdb_world_flag, golfer_id), offsetof(stdb_world_flag, flag)});
    describe(sizeof(stdb_room), {offsetof(stdb_room, room_id), offsetof(stdb_room, course_id), offsetof(stdb_room, player_count), offsetof(stdb_room, world_id), offsetof(stdb_room, kind)});
    describe(sizeof(stdb_room_member),
             {offsetof(stdb_room_member, account_id), offsetof(stdb_room_member, room_id), offsetof(stdb_room_member, group_id),
              offsetof(stdb_room_member, round_strokes),
              offsetof(stdb_room_member, round_strokes_count), offsetof(stdb_room_member, round_started)});
    describe(sizeof(stdb_golf_group), {offsetof(stdb_golf_group, group_id), offsetof(stdb_golf_group, room_id),
                                       offsetof(stdb_golf_group, member_count)});
    describe(sizeof(stdb_setup), {offsetof(stdb_setup, aim), offsetof(stdb_setup, face_open),
                                  offsetof(stdb_setup, ball_position), offsetof(stdb_setup, backswing)});
    describe(sizeof(stdb_motion),
             {offsetof(stdb_motion, mode), offsetof(stdb_motion, x), offsetof(stdb_motion, y),
              offsetof(stdb_motion, z), offsetof(stdb_motion, yaw), offsetof(stdb_motion, speed),
              offsetof(stdb_motion, turn_rate), offsetof(stdb_motion, client_time), offsetof(stdb_motion, setup),
              offsetof(stdb_motion, crouched)});
    describe(sizeof(stdb_avatar_motion),
             {offsetof(stdb_avatar_motion, account_id), offsetof(stdb_avatar_motion, room_id),
              offsetof(stdb_avatar_motion, motion), offsetof(stdb_avatar_motion, server_time_micros)});
    describe(sizeof(stdb_ride_motion), {offsetof(stdb_ride_motion, rider), offsetof(stdb_ride_motion, owner),
                                        offsetof(stdb_ride_motion, motion), offsetof(stdb_ride_motion, server_time_micros)});
    describe(sizeof(stdb_ball), {offsetof(stdb_ball, account_id), offsetof(stdb_ball, room_id), offsetof(stdb_ball, hole),
                                 offsetof(stdb_ball, x), offsetof(stdb_ball, y), offsetof(stdb_ball, z),
                                 offsetof(stdb_ball, stroke_count), offsetof(stdb_ball, last_shot_at),
                                 offsetof(stdb_ball, in_cup), offsetof(stdb_ball, kind), offsetof(stdb_ball, tee_mat)});
    describe(sizeof(stdb_golf_cart),
             {offsetof(stdb_golf_cart, owner), offsetof(stdb_golf_cart, room_id), offsetof(stdb_golf_cart, x),
              offsetof(stdb_golf_cart, y), offsetof(stdb_golf_cart, z), offsetof(stdb_golf_cart, yaw),
              offsetof(stdb_golf_cart, passenger), offsetof(stdb_golf_cart, condition), offsetof(stdb_golf_cart, driver), offsetof(stdb_golf_cart, motion_id)});
    describe(sizeof(stdb_shot), {offsetof(stdb_shot, stroke), offsetof(stdb_shot, aim_angle), offsetof(stdb_shot, face_open),
                                 offsetof(stdb_shot, ball_position), offsetof(stdb_shot, backswing),
                                 offsetof(stdb_shot, strike_timing), offsetof(stdb_shot, club_id),
                                 offsetof(stdb_shot, shot_at), offsetof(stdb_shot, flags_out),
                                 offsetof(stdb_shot, flags_out_count)});
    describe(sizeof(stdb_move_run),
             {offsetof(stdb_move_run, kind), offsetof(stdb_move_run, steps), offsetof(stdb_move_run, forward),
              offsetof(stdb_move_run, side), offsetof(stdb_move_run, run), offsetof(stdb_move_run, jump),
              offsetof(stdb_move_run, yaw), offsetof(stdb_move_run, steer), offsetof(stdb_move_run, paced),
              offsetof(stdb_move_run, crouched)});
    describe(sizeof(stdb_steps), {offsetof(stdb_steps, first_step), offsetof(stdb_steps, start_x),
                                  offsetof(stdb_steps, start_y), offsetof(stdb_steps, start_z),
                                  offsetof(stdb_steps, runs), offsetof(stdb_steps, run_count)});
    describe(sizeof(stdb_snapshot_entry),
             {offsetof(stdb_snapshot_entry, kind), offsetof(stdb_snapshot_entry, x), offsetof(stdb_snapshot_entry, y),
              offsetof(stdb_snapshot_entry, z), offsetof(stdb_snapshot_entry, yaw), offsetof(stdb_snapshot_entry, shooter)});
    describe(sizeof(stdb_skill_level), {offsetof(stdb_skill_level, skill_id), offsetof(stdb_skill_level, level)});
    describe(sizeof(stdb_shot_event),
             {offsetof(stdb_shot_event, room_id), offsetof(stdb_shot_event, account_id), offsetof(stdb_shot_event, hole),
              offsetof(stdb_shot_event, shot), offsetof(stdb_shot_event, hand), offsetof(stdb_shot_event, natural_shape),
              offsetof(stdb_shot_event, spread_scale), offsetof(stdb_shot_event, levels),
              offsetof(stdb_shot_event, levels_count), offsetof(stdb_shot_event, seed), offsetof(stdb_shot_event, snapshot),
              offsetof(stdb_shot_event, snapshot_count), offsetof(stdb_shot_event, start_x),
              offsetof(stdb_shot_event, start_y), offsetof(stdb_shot_event, start_z), offsetof(stdb_shot_event, rest_x),
              offsetof(stdb_shot_event, rest_y), offsetof(stdb_shot_event, rest_z), offsetof(stdb_shot_event, holed)});
    describe(sizeof(stdb_dap), {offsetof(stdb_dap, id), offsetof(stdb_dap, room_id), offsetof(stdb_dap, first), offsetof(stdb_dap, second), offsetof(stdb_dap, started_at), offsetof(stdb_dap, first_x), offsetof(stdb_dap, first_y), offsetof(stdb_dap, first_z), offsetof(stdb_dap, second_x), offsetof(stdb_dap, second_y), offsetof(stdb_dap, second_z)});
    describe(sizeof(stdb_item_use_event), {offsetof(stdb_item_use_event, room_id), offsetof(stdb_item_use_event, account_id),
                                           offsetof(stdb_item_use_event, item_id)});
    describe(sizeof(stdb_loadout_mount), {offsetof(stdb_loadout_mount, mount), offsetof(stdb_loadout_mount, item_id)});
    describe(sizeof(stdb_player_loadout),
             {offsetof(stdb_player_loadout, account_id), offsetof(stdb_player_loadout, cart),
              offsetof(stdb_player_loadout, cart_mounts), offsetof(stdb_player_loadout, cart_mounts_count),
              offsetof(stdb_player_loadout, smokable), offsetof(stdb_player_loadout, drinkable),
              offsetof(stdb_player_loadout, bag)});
    describe(sizeof(stdb_player_bag),
             {offsetof(stdb_player_bag, account_id), offsetof(stdb_player_bag, room_id), offsetof(stdb_player_bag, place),
              offsetof(stdb_player_bag, seat), offsetof(stdb_player_bag, cart_owner), offsetof(stdb_player_bag, x),
              offsetof(stdb_player_bag, y),
              offsetof(stdb_player_bag, z), offsetof(stdb_player_bag, yaw), offsetof(stdb_player_bag, club_in_hand)});
    describe(sizeof(stdb_green_flag),
             {offsetof(stdb_green_flag, id), offsetof(stdb_green_flag, room_id), offsetof(stdb_green_flag, hole)});
    describe(sizeof(stdb_running_effect),
             {offsetof(stdb_running_effect, id), offsetof(stdb_running_effect, account_id), offsetof(stdb_running_effect, item_id),
              offsetof(stdb_running_effect, target), offsetof(stdb_running_effect, bucket), offsetof(stdb_running_effect, value),
              offsetof(stdb_running_effect, duration), offsetof(stdb_running_effect, seconds),
              offsetof(stdb_running_effect, started_at_ms), offsetof(stdb_running_effect, ends_at_ms)});
    describe(sizeof(stdb_link_code), {offsetof(stdb_link_code, code), offsetof(stdb_link_code, expires_at_micros)});
    describe(sizeof(stdb_link_status), {offsetof(stdb_link_status, result), offsetof(stdb_link_status, at_micros)});
    describe(sizeof(stdb_server_protocol), {offsetof(stdb_server_protocol, version)});
    describe(sizeof(stdb_server_content), {offsetof(stdb_server_content, hash)});
    describe(sizeof(stdb_tee_slot), {offsetof(stdb_tee_slot, slot_id), offsetof(stdb_tee_slot, course_id),
        offsetof(stdb_tee_slot, world_id), offsetof(stdb_tee_slot, room_id), offsetof(stdb_tee_slot, scheduled_at), offsetof(stdb_tee_slot, booked_count)});
    describe(sizeof(stdb_tee_reservation), {offsetof(stdb_tee_reservation, reservation_id), offsetof(stdb_tee_reservation, slot_id),
        offsetof(stdb_tee_reservation, started_at), offsetof(stdb_tee_reservation, grouped), offsetof(stdb_tee_reservation, completed), offsetof(stdb_tee_reservation, revision), offsetof(stdb_tee_reservation, course_id),
        offsetof(stdb_tee_reservation, world_id), offsetof(stdb_tee_reservation, room_id), offsetof(stdb_tee_reservation, scheduled_at)});
    describe(sizeof(stdb_tee_book_status), {offsetof(stdb_tee_book_status, course_id), offsetof(stdb_tee_book_status, server_now)});
    out.push_back(sizeof(stdb_row));
    describe(sizeof(stdb_event),
             {offsetof(stdb_event, kind), offsetof(stdb_event, text), offsetof(stdb_event, reducer),
              offsetof(stdb_event, subscription), offsetof(stdb_event, login_method), offsetof(stdb_event, retrying),
              offsetof(stdb_event, table), offsetof(stdb_event, change), offsetof(stdb_event, row)});
    out.push_back(sizeof(stdb_event_kind));
    out.push_back(sizeof(bool));
    return out;
}

std::string failure_id(const std::string& failure) {
    return failure.substr(0, failure.find(':'));
}

void net_client::client_deleter::operator()(stdb_client* client) const {
    stdb_destroy(client);
}

net_client::net_client(stdb_client* client, const std::uint64_t content_hash) : client_(client), content_hash_(content_hash) {}

std::optional<net_client> net_client::create(const online_config& config,
                                             const std::string& page_signed_in,
                                             const std::string& page_failed,
                                             const std::uint64_t content_hash) {
    if (!layouts_agree()) {
        return std::nullopt;
    }
    const stdb_config bridge_config{view(config.server_uri),
                                    view(config.database),
                                    view(config.auth_issuer),
                                    view(config.auth_client_id),
                                    view(config.auth_scopes),
                                    view(config.auth_authorization_endpoint),
                                    view(config.auth_token_endpoint),
                                    view(page_signed_in),
                                    view(page_failed),
                                    config.anonymous};
    stdb_client* client = stdb_create(&bridge_config);
    if (client == nullptr) {
        return std::nullopt;
    }
    return net_client(client, content_hash);
}

void net_client::begin_login() {
    start_login(false);
}

void net_client::begin_silent_login() {
    start_login(true);
}

std::int64_t net_client::local_now() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_).count();
}

void net_client::start_login(const bool silent_only) {
    if (stdb_begin_login(client_.get(), silent_only)) {
        failure_.clear();
        server_refused_ = false;
        status_ = net_status::signing_in;
    }
}

void net_client::cancel_login() {
    stdb_cancel_login(client_.get());
    if (status_ != net_status::connected) {
        status_ = net_status::signed_out;
    }
}

void net_client::sign_out(game_state& state) {
    stdb_sign_out(client_.get());
    forget_session(state);
    status_ = net_status::signed_out;
}

void net_client::claim_name(const std::string& name) {
    if (status_ == net_status::connected) {
        stdb_claim_name(client_.get(), name.data(), name.size());
    }
}

void net_client::join_course(const std::string& course_id, const bool takes_cart) {
    if (status_ == net_status::connected) {
        stdb_join_course(client_.get(), course_id.data(), course_id.size(), takes_cart);
    }
}

void net_client::leave_room() {
    if (status_ == net_status::connected) {
        stdb_leave_room(client_.get(), room_id_);
    }
}

void net_client::create_link_code() {
    if (status_ == net_status::connected) {
        stdb_create_link_code(client_.get());
    }
}

void net_client::redeem_link_code(const std::string& code) {
    if (status_ == net_status::connected) {
        stdb_redeem_link_code(client_.get(), code.data(), code.size());
    }
}

std::optional<reducer_failure> net_client::take_reducer_failure() {
    if (reducer_failures_.empty()) {
        return std::nullopt;
    }
    reducer_failure failure = std::move(reducer_failures_.front());
    reducer_failures_.pop_front();
    return failure;
}

void net_client::update(game_state& state) {
    stdb_frame_tick(client_.get());

    std::array<stdb_event, 64> events{};
    std::size_t count = 0;
    while ((count = stdb_poll(client_.get(), events.data(), events.size())) > 0) {
        for (std::size_t i = 0; i < count; ++i) {
            handle(events[i], state);
        }
    }
    if (protocol_applied_ && content_subscription_ == 0 && !server_refused_) {
        refuse_server(net_failure_outdated_game, "empty server_protocol", state);
    } else if (content_applied_ && status_ != net_status::connected && !server_refused_) {
        refuse_server(net_failure_content_mismatch, "empty server_content_hash", state);
    }
    protocol_applied_ = false;
    content_applied_ = false;

    if (self_applied_) {
        state.online.reservation = reservation_;
        state.online.book_course_id = book_course_;
        state.online.book_ready = book_acknowledged_;
        state.online.tee_slots.clear();
        if (book_acknowledged_) {
            for (const auto& [id, slot] : tee_slots_) {
                if (slot.course_id == book_course_) { state.online.tee_slots.emplace(id, slot); }
            }
        }
    }
    deliver_progress(state);
    room_.publish(state);
    state.online.effects.clear();
    for (const auto& [id, effect] : effects_) {
        state.online.effects.push_back(effect);
    }
    state.online.status = status_;
    state.online.server_now = clock_.now(local_now());
    send_commands(state);
}

void net_client::deliver_progress(game_state& state) {
    if (progress_changed_ && self_applied_) {
        receive_online_progress(state, progress());
        progress_changed_ = false;
    }
}

void net_client::send_commands(game_state& state) {
    if (status_ != net_status::connected) {
        state.motion_sync.last_sent.reset();
        state.motion_sync.queued.reset();
        state.net_commands.clear();  // motion remains due; actions are never replayed into another session
        return;
    }
    stdb_client* client = client_.get();
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    for (const net_command& command : state.net_commands) {
        if (command.room_id != 0 && command.room_id != room_id_) { continue; }
        switch (command.type) {
        case net_command_type::motion: {
            const net_motion& m = command.motion;
            const stdb_setup setup{m.setup.aim, m.setup.face_open, m.setup.ball_position, m.setup.backswing};
            const stdb_motion motion{static_cast<std::uint8_t>(m.mode), m.position.x, m.position.y, m.position.z,
                                     m.yaw, m.speed, m.turn_rate, now, setup, m.crouched};
            std::vector<stdb_move_run> runs;
            runs.reserve(command.runs.size());
            for (const move_run& run : command.runs) {
                const move_input& in = run.input;
                runs.push_back(stdb_move_run{static_cast<std::uint8_t>(in.kind), run.steps, in.forward, in.side, in.run,
                                             in.jump, in.yaw, in.steer, in.paced, in.crouched});
            }
            const stdb_steps steps{command.first_step, command.start.x, command.start.y, command.start.z, runs.data(),
                                   runs.size()};
            if (stdb_update_motion(client, room_id_, &motion, &steps)) {
                motion_sent(state, command, now);
            } else {
                state.motion_sync.last_sent.reset();
                state.motion_sync.queued.reset();
            }
            now = std::nextafter(now, std::numeric_limits<double>::infinity());
            break;
        }
        case net_command_type::ride_motion: {
            const net_motion& m = command.motion;
            const stdb_motion motion{static_cast<std::uint8_t>(m.mode), m.position.x, m.position.y, m.position.z,
                                     m.yaw, m.speed, m.turn_rate, now, {}, false};
            stdb_update_ride(client, room_id_, &motion);
            break;
        }
        case net_command_type::take_shot: {
            const shot_setup& s = command.shot;
            const std::vector<std::int32_t> flags_out(command.flags_out.begin(), command.flags_out.end());
            const stdb_shot shot{command.stroke, s.aim_angle,      s.face_open, s.ball_position,   s.backswing,
                                 s.strike_timing, view(s.club_id), s.shot_at, flags_out.data(), flags_out.size()};
            stdb_take_shot(client, room_id_, &shot);
            break;
        }
        case net_command_type::set_flag:
            stdb_set_flag(client, room_id_, static_cast<std::int32_t>(command.hole_index), command.flag_out);
            break;
        case net_command_type::place_bag: {
            const bag_place& bag = command.bag;
            stdb_place_bag(client, room_id_, static_cast<std::uint8_t>(bag.where), static_cast<std::uint8_t>(bag.seat), bag.cart_owner,
                           bag.position.x, bag.position.y, bag.position.z, bag.yaw);
            break;
        }
        case net_command_type::take_club:
            stdb_take_club(client, room_id_, command.club_id.data(), command.club_id.size());
            break;
        case net_command_type::set_golfer:
            stdb_set_golfer(client, static_cast<std::uint8_t>(command.golfer.hand), command.golfer.natural,
                            command.golfer.colour);
            break;
        case net_command_type::place_practice_ball:
            stdb_place_practice_ball(client, command.room_id, command.placed.x, command.placed.y, command.placed.z);
            break;
        case net_command_type::tee_up:
            stdb_tee_up(client, room_id_, static_cast<std::int32_t>(command.hole_index), command.give_up);
            break;
        case net_command_type::claim_collectible:
            stdb_claim_collectible(client, room_id_, command.collectible_id.data(), command.collectible_id.size());
            break;
        case net_command_type::dap:
            stdb_dap_up(client, command.room_id, command.target_account); break;
        case net_command_type::use_item:
            stdb_use_item(client, room_id_, command.item_id.data(), command.item_id.size());
            break;
        case net_command_type::pick_up_ball:
            stdb_pick_up_ball(client, room_id_);
            break;
        case net_command_type::open_tee_book:
            book_course_ = command.course_id;
            book_acknowledged_ = false;
            book_after_ = last_book_status_at_;
            state.online.book_ready = false;
            state.online.tee_slots.clear();
            stdb_open_tee_book(client, command.room_id, command.course_id.data(), command.course_id.size());
            break;
        case net_command_type::book_tee:
            stdb_book_tee(client, command.room_id, command.reservation_id, command.course_id.data(), command.course_id.size(), command.scheduled_at);
            break;
        case net_command_type::cancel_tee:
            stdb_cancel_tee(client, command.room_id, command.reservation_id);
            break;
        case net_command_type::take_bus:
            stdb_take_bus(client, command.room_id, command.reservation_id, command.takes_cart);
            break;
        case net_command_type::return_to_hub:
            stdb_return_to_hub(client, command.room_id);
            break;
        case net_command_type::start_booked_round:
            stdb_start_booked_round(client, command.room_id, command.reservation_id);
            break;
        case net_command_type::leave_group:
            stdb_leave_group(client, command.room_id, command.reservation_id);
            break;
        case net_command_type::give_up_hole:
            stdb_give_up_hole(client, room_id_);
            break;
        case net_command_type::take_wheel:
            stdb_take_wheel(client, room_id_);
            break;
        case net_command_type::leave_wheel:
            stdb_leave_wheel(client, room_id_);
            break;
        case net_command_type::board_cart:
            stdb_board_cart(client, room_id_, command.cart_owner);
            break;
        case net_command_type::leave_cart:
            stdb_leave_cart(client, room_id_);
            break;
        case net_command_type::right_cart:
            stdb_right_cart(client, room_id_, command.cart_owner);
            break;
        }
    }
    state.net_commands.clear();
}

void net_client::handle(const stdb_event& event, game_state& state) {
    if (server_refused_ && event.kind != STDB_EVENT_DISCONNECTED) {
        return;
    }
    switch (event.kind) {
    case STDB_EVENT_LOGIN_WAITING_FOR_BROWSER:
        status_ = net_status::waiting_for_browser;
        break;
    case STDB_EVENT_LOGIN_FAILED:
        status_ = net_status::failed;
        failure_ = text_of(event.text);
        break;
    case STDB_EVENT_SIGNED_IN:
        status_ = net_status::connecting;
        guest_ = event.login_method == STDB_LOGIN_ANONYMOUS;
        break;
    case STDB_EVENT_CONNECTED:
        // Connected once the server's protocol and content match (below).
        // Nothing else is read before: another protocol's rows may not decode.
        forget_session(state);
        status_ = net_status::connecting;
        protocol_subscription_ = subscribe(client_.get(), {"SELECT * FROM server_protocol"});
        break;
    case STDB_EVENT_DISCONNECTED:
        forget_session(state);
        if (server_refused_) {
            break;  // refuse_server's own disconnect: its failure stands
        }
        status_ = event.retrying ? net_status::signing_in : net_status::signed_out;
        failure_ = text_of(event.text);
        break;
    case STDB_EVENT_SUBSCRIPTION_APPLIED:
        if (protocol_subscription_ != 0 && event.subscription == protocol_subscription_) {
            protocol_applied_ = true;
            break;
        }
        if (content_subscription_ != 0 && event.subscription == content_subscription_) {
            content_applied_ = true;
            break;
        }
        if (room_subscription_ != 0 && event.subscription == room_subscription_) {
            room_.ready(state);
        }
        if (event.subscription == self_subscription_) {
            self_applied_ = true;
            progress_changed_ = true;
        }
        break;
    case STDB_EVENT_SUBSCRIPTION_FAILED:
        if (protocol_subscription_ != 0 && event.subscription == protocol_subscription_) {
            refuse_server(net_failure_outdated_game, "no server_protocol", state);
            break;
        }
        if (content_subscription_ != 0 && event.subscription == content_subscription_) {
            refuse_server(net_failure_content_mismatch, "no server_content_hash", state);
            break;
        }
        if (room_subscription_ != 0 && event.subscription == room_subscription_) {
            room_.clear(state);
            status_ = net_status::failed;
        }
        failure_ = text_of(event.text);
        break;
    case STDB_EVENT_ROW:
        handle_row(event, state);
        break;
    case STDB_EVENT_REDUCER_FAILED: {
        reducer_failure failure{text_of(event.reducer), text_of(event.text)};
        if (gameplay_reducer(failure.reducer)) {
            state.online.refusals.push_back(failure);
        } else {
            reducer_failures_.push_back(failure);
        }
        // A refused claim leaves the collectible as it was: offer it again.
        // One client's reducers run in order, so once the claims answered
        // before it have left the list (with their progress), it is the oldest.
        if (failure.reducer == reducer_claim_collectible) {
            deliver_progress(state);
            if (!state.online.pending_claims.empty()) {
                state.online.pending_claims.erase(state.online.pending_claims.begin());
            }
        }
        break;
    }
    }
}

void net_client::handle_row(const stdb_event& event, game_state& state) {
    const stdb_row& row = event.row;
    const stdb_row_change change = event.change;
    if (server_refused_ || (status_ != net_status::connected && event.table != STDB_TABLE_SERVER_PROTOCOL &&
                           event.table != STDB_TABLE_SERVER_CONTENT_HASH)) {
        return;
    }
    switch (event.table) {
    case STDB_TABLE_SERVER_PROTOCOL:
        if (change == STDB_ROW_DELETE) {
            break;
        }
        if (row.server_protocol.version != protocol_version) {
            refuse_server(net_failure_outdated_game, "server " + std::to_string(row.server_protocol.version) + ", game " +
                          std::to_string(protocol_version), state);
        } else if (content_subscription_ == 0) {
            content_subscription_ = subscribe(client_.get(), {"SELECT * FROM server_content_hash"});
        }
        break;
    case STDB_TABLE_SERVER_CONTENT_HASH:
        if (content_subscription_ == 0 || change == STDB_ROW_DELETE) {
            break;
        }
        if (row.server_content.hash != content_hash_) {
            refuse_server(net_failure_content_mismatch, "server " + std::to_string(row.server_content.hash) + ", game " +
                          std::to_string(content_hash_), state);
        } else if (status_ != net_status::connected) {
            status_ = net_status::connected;
            subscribe(client_.get(), {"SELECT * FROM my_account", "SELECT * FROM my_link_code", "SELECT * FROM my_link_status"});
        }
        break;
    case STDB_TABLE_MY_ACCOUNT:
        if (change == STDB_ROW_DELETE) {
            break;
        }
        // A new account id (the first, or another after linking this login
        // to an account) starts its progress over: accounts never mix.
        if (row.player.account_id != account_id_) {
            forget_progress(state);
            account_id_ = row.player.account_id;
            subscribe_self();
        }
        state.online.account_id = account_id_;
        state.online.display_name = text_of(row.player.display_name);
        break;
    case STDB_TABLE_PLAYER:
        // My own row comes once, from my own subscription: the room's holds
        // it again without a second insert, so it is read here, not there.
        if (row.player.account_id == account_id_ && account_id_ != 0 && change != STDB_ROW_DELETE) {
            state.online.golfer = golfer_row{hand_of(row.player.hand), row.player.natural_shape, row.player.favourite_colour};
        }
        room_.apply(event, account_id_, local_now(), clock_, state);
        break;
    case STDB_TABLE_TEE_SLOT: {
        const auto& v = row.tee_slot;
        const tee_slot_info slot{v.slot_id, text_of(v.course_id), v.world_id, v.room_id, v.scheduled_at, v.booked_count};
        if (change == STDB_ROW_DELETE) {
            const auto found = tee_slots_.find(v.slot_id);
            if (found != tee_slots_.end() && found->second.booked_count == slot.booked_count) { tee_slots_.erase(found); }
        } else { tee_slots_.insert_or_assign(v.slot_id, slot); }
        break;
    }
    case STDB_TABLE_MY_TEE_RESERVATION: {
        const auto& v = row.tee_reservation;
        if (change == STDB_ROW_DELETE) {
            if (reservation_ && reservation_->reservation_id == v.reservation_id && reservation_->revision == v.revision) { reservation_.reset(); }
        } else if (!reservation_ || v.reservation_id > reservation_->reservation_id ||
                   (v.reservation_id == reservation_->reservation_id && v.revision >= reservation_->revision)) {
            reservation_ = tee_reservation_info{v.reservation_id, v.slot_id, v.started_at, v.grouped, v.completed,
                v.revision, text_of(v.course_id), v.world_id, v.room_id, v.scheduled_at};
        }
        break;
    }
    case STDB_TABLE_MY_TEE_BOOK_STATUS:
        if (change != STDB_ROW_DELETE) {
            clock_.sample(row.tee_book_status.server_now, local_now());
            if (text_of(row.tee_book_status.course_id) == book_course_ && row.tee_book_status.server_now > book_after_) {
                book_acknowledged_ = true;
            }
            last_book_status_at_ = std::max(last_book_status_at_, row.tee_book_status.server_now);
        }
        break;
    case STDB_TABLE_MY_LINK_CODE:
        if (change == STDB_ROW_DELETE) {
            link_code_.reset();
        } else {
            link_code_ = link_code_info{text_of(row.link_code.code), row.link_code.expires_at_micros};
        }
        break;
    case STDB_TABLE_MY_LINK_STATUS:
        if (change != STDB_ROW_DELETE) {
            link_result_ = text_of(row.link_status.result);
            ++link_results_;
        }
        break;
    // My progress: the views give my rows alone.
    case STDB_TABLE_MY_GOLFER_PROGRESS:
        if (change != STDB_ROW_DELETE) {
            holes_completed_ = row.golfer_progress.holes_completed;
            progress_changed_ = true;
        }
        break;
    case STDB_TABLE_MY_SKILLS:
        apply_view_change(skills_, row.golfer_skill.id, change, {text_of(row.golfer_skill.skill_id), row.golfer_skill.xp});
        progress_changed_ = true;
        break;
    case STDB_TABLE_MY_COMPLETED_COURSES:
        apply_view_change(completed_courses_, row.completed_course.id, change, text_of(row.completed_course.course_id));
        progress_changed_ = true;
        break;
    case STDB_TABLE_MY_COLLECTED: {
        const stdb_collected& c = row.collected;
        apply_view_change(collected_, c.id, change,
                          collected_state{text_of(c.collectible_id), c.repeatable, c.claim_count, c.claimed_at_holes_completed});
        progress_changed_ = true;
        break;
    }
    case STDB_TABLE_MY_WORLD_FLAGS:
        apply_view_change(world_flags_, row.world_flag.id, change, text_of(row.world_flag.flag));
        progress_changed_ = true;
        break;
    case STDB_TABLE_RUNNING_EFFECT:
        if (row.running_effect.account_id == account_id_) {
            const stdb_running_effect& e = row.running_effect;
            item_effect effect;
            effect.target = text_of(e.target);
            effect.bucket = static_cast<effect_bucket>(e.bucket);
            effect.value = e.value;
            effect.duration = static_cast<effect_duration>(e.duration);
            effect.seconds = e.seconds;
            apply_change(effects_, e.id, change, active_effect{text_of(e.item_id), effect, e.started_at_ms, e.ends_at_ms});
        }
        break;
    case STDB_TABLE_ROOM_MEMBER:
        if (row.room_member.account_id == account_id_) {
            if (change == STDB_ROW_DELETE && row.room_member.room_id != room_id_) {
                break;
            }
            const std::uint64_t room = change == STDB_ROW_DELETE ? 0 : row.room_member.room_id;
            if (room != room_id_) {
                clear_online_presentation(state);
                state.online.room.reset();
                room_.begin(room, state);
                reset_motion_sync(state);
                state.net_commands.clear();
                state.remote_avatars.clear();
                state.ride_shown.reset();
                state.ride.reset();
                subscribe_room(room);
            }
        }
        room_.apply(event, account_id_, local_now(), clock_, state);
        break;
    case STDB_TABLE_ROOM:
        if (row.room.room_id == room_id_ && room_id_ != 0) {
            if (change == STDB_ROW_DELETE) {
                clear_online_presentation(state);
                state.online.room.reset();
            } else {
                state.online.room = online_room{room_id_, text_of(row.room.course_id), row.room.player_count, row.room.world_id, static_cast<place_kind>(row.room.kind)};
            }
        }
        break;
    default:
        room_.apply(event, account_id_, local_now(), clock_, state);
        break;
    }
}

// My progress through the my_* views (the tables are Private). Not my hole
// scores: they keep every hole ever scored, and nothing shows them yet.
void net_client::subscribe_self() {
    self_applied_ = false;
    const std::string id = std::to_string(account_id_);
    self_subscription_ = subscribe(client_.get(), {
                                                      "SELECT * FROM player WHERE account_id = " + id,
                                                      "SELECT * FROM my_golfer_progress",
                                                      "SELECT * FROM my_tee_reservation",
                                                      "SELECT * FROM my_tee_book_status",
                                                      "SELECT * FROM my_tee_slots",
                                                      "SELECT * FROM my_skills",
                                                      "SELECT * FROM my_completed_courses",
                                                      "SELECT * FROM my_collected",
                                                      "SELECT * FROM my_world_flags",
                                                      "SELECT * FROM running_effect WHERE account_id = " + id,
                                                      "SELECT * FROM room_member WHERE account_id = " + id,
                                                  });
}

void net_client::subscribe_room(const std::uint64_t room_id) {
    if (room_subscription_ != 0) {
        stdb_unsubscribe(client_.get(), room_subscription_);
        room_subscription_ = 0;
    }
    room_id_ = room_id;
    if (room_id == 0) {
        return;
    }
    const std::string id = std::to_string(room_id);
    room_subscription_ = subscribe(
        client_.get(),
        {
            "SELECT * FROM room WHERE room_id = " + id,
            "SELECT * FROM room_member WHERE room_id = " + id,
            "SELECT * FROM golf_group WHERE room_id = " + id,
            "SELECT * FROM my_avatar_motion",
            "SELECT * FROM ball WHERE room_id = " + id,
            "SELECT * FROM my_golf_carts",
            "SELECT * FROM player_bag WHERE room_id = " + id,
            "SELECT * FROM green_flag WHERE room_id = " + id,
            "SELECT * FROM shot_event WHERE room_id = " + id,
            "SELECT * FROM item_use_event WHERE room_id = " + id,
            "SELECT * FROM dap_pair WHERE room_id = " + id,
            // Mine alone: the cart I ride in as its driver sends it.
            "SELECT * FROM ride_motion WHERE rider = " + std::to_string(account_id_),
            // Room members' names and what they have equipped.
            "SELECT player.* FROM player JOIN room_member ON player.account_id = room_member.account_id WHERE room_member.room_id = " + id,
            "SELECT player_loadout.* FROM player_loadout JOIN room_member ON player_loadout.account_id = room_member.account_id "
            "WHERE room_member.room_id = " + id,
        });
}

void net_client::refuse_server(const std::string& failure, const std::string& detail, game_state& state) {
    server_refused_ = true;
    forget_session(state);
    status_ = net_status::failed;
    failure_ = failure + ": " + detail;
    stdb_disconnect(client_.get());
}

void net_client::forget_session(game_state& state) {
    protocol_subscription_ = 0;
    content_subscription_ = 0;
    protocol_applied_ = false;
    content_applied_ = false;
    self_subscription_ = 0;
    room_subscription_ = 0;
    link_code_.reset();
    forget_progress(state);
}

void net_client::forget_progress(game_state& state) {
    if (self_subscription_ != 0) {
        stdb_unsubscribe(client_.get(), self_subscription_);
        self_subscription_ = 0;
    }
    if (room_subscription_ != 0) {
        stdb_unsubscribe(client_.get(), room_subscription_);
        room_subscription_ = 0;
    }
    account_id_ = 0;
    room_id_ = 0;
    clock_ = server_clock{};
    self_applied_ = false;
    progress_changed_ = false;
    holes_completed_ = 0;
    skills_.clear();
    completed_courses_.clear();
    collected_.clear();
    world_flags_.clear();
    effects_.clear();
    reservation_.reset();
    tee_slots_.clear();
    book_course_.clear();
    book_after_ = 0;
    last_book_status_at_ = 0;
    book_acknowledged_ = false;
    room_.clear(state);
    state.hub_flow = hub_flow_state{};
    clear_online_presentation(state);
    state.online = online_view{};
    reset_motion_sync(state);
    state.net_commands.clear();
    state.remote_avatars.clear();
    state.ride_shown.reset();
}

save_data net_client::progress() const {
    save_data progress;
    progress.holes_completed = holes_completed_;
    for (const auto& [id, skill] : skills_) {
        progress.skills[skill.first].xp = skill.second;
    }
    for (const auto& [id, course] : completed_courses_) {
        progress.completed_course_ids.push_back(course);
    }
    for (const auto& [id, claim] : collected_) {
        if (!claim.repeatable) {
            progress.collected_ids.push_back(claim.collectible_id);
            continue;
        }
        repeatable_collectible_state& repeatable = progress.repeatable_collectibles[claim.collectible_id];
        repeatable.claim_count = claim.claim_count;
        if (claim.claimed_at_holes_completed >= 0) {
            repeatable.claimed_at_holes_completed = claim.claimed_at_holes_completed;
        }
    }
    for (const auto& [id, flag] : world_flags_) {
        progress.world_flags.push_back(flag);
    }
    return progress;
}
