#include "net/net_client.h"

#include "game/mode_dispatch.h"
#include "game/net_types.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {
stdb_string view(const std::string& text) {
    return stdb_string{text.data(), text.size()};
}

std::string text_of(const stdb_string& text) {
    return text.data != nullptr ? std::string(text.data, text.len) : std::string();
}

// The reducers net_commands call: the game answers their refusals.
bool gameplay_reducer(const std::string& reducer) {
    for (const char* name : {"update_motion", "take_shot", "enter_hole", "claim_collectible", "emote", "retee",
                             "pick_up_ball", "return_to_hub", "create_group", "join_group", "leave_group"}) {
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

template <typename Map>
void apply_change(Map& rows, const std::uint64_t id, const stdb_row_change change, typename Map::mapped_type value) {
    if (change == STDB_ROW_DELETE) {
        rows.erase(id);
    } else {
        rows[id] = std::move(value);
    }
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
                                   offsetof(stdb_player, online), offsetof(stdb_player, holes_completed)});
    describe(sizeof(stdb_player_skill), {offsetof(stdb_player_skill, id), offsetof(stdb_player_skill, account_id),
                                         offsetof(stdb_player_skill, skill_id), offsetof(stdb_player_skill, xp)});
    describe(sizeof(stdb_hole_score),
             {offsetof(stdb_hole_score, id), offsetof(stdb_hole_score, account_id), offsetof(stdb_hole_score, course_id),
              offsetof(stdb_hole_score, hole_index), offsetof(stdb_hole_score, strokes), offsetof(stdb_hole_score, par)});
    describe(sizeof(stdb_completed_course), {offsetof(stdb_completed_course, id), offsetof(stdb_completed_course, account_id),
                                             offsetof(stdb_completed_course, course_id)});
    describe(sizeof(stdb_collected),
             {offsetof(stdb_collected, id), offsetof(stdb_collected, account_id), offsetof(stdb_collected, collectible_id),
              offsetof(stdb_collected, repeatable), offsetof(stdb_collected, claim_count),
              offsetof(stdb_collected, claimed_at_holes_completed)});
    describe(sizeof(stdb_world_flag),
             {offsetof(stdb_world_flag, id), offsetof(stdb_world_flag, account_id), offsetof(stdb_world_flag, flag)});
    describe(sizeof(stdb_room), {offsetof(stdb_room, room_id), offsetof(stdb_room, course_id), offsetof(stdb_room, player_count)});
    describe(sizeof(stdb_room_member),
             {offsetof(stdb_room_member, account_id), offsetof(stdb_room_member, room_id), offsetof(stdb_room_member, group_id),
              offsetof(stdb_room_member, zone), offsetof(stdb_room_member, hole_started_at_micros),
              offsetof(stdb_room_member, round_strokes),
              offsetof(stdb_room_member, round_strokes_count)});
    describe(sizeof(stdb_golf_group), {offsetof(stdb_golf_group, group_id), offsetof(stdb_golf_group, room_id),
                                       offsetof(stdb_golf_group, leader), offsetof(stdb_golf_group, member_count)});
    describe(sizeof(stdb_motion),
             {offsetof(stdb_motion, zone), offsetof(stdb_motion, mode), offsetof(stdb_motion, x), offsetof(stdb_motion, y),
              offsetof(stdb_motion, z), offsetof(stdb_motion, yaw), offsetof(stdb_motion, speed),
              offsetof(stdb_motion, turn_rate), offsetof(stdb_motion, client_time)});
    describe(sizeof(stdb_avatar_motion),
             {offsetof(stdb_avatar_motion, account_id), offsetof(stdb_avatar_motion, room_id),
              offsetof(stdb_avatar_motion, motion), offsetof(stdb_avatar_motion, server_time_micros)});
    describe(sizeof(stdb_ball), {offsetof(stdb_ball, account_id), offsetof(stdb_ball, room_id), offsetof(stdb_ball, zone),
                                 offsetof(stdb_ball, x), offsetof(stdb_ball, y), offsetof(stdb_ball, z),
                                 offsetof(stdb_ball, stroke_count), offsetof(stdb_ball, last_wind_time)});
    describe(sizeof(stdb_shot), {offsetof(stdb_shot, stroke), offsetof(stdb_shot, aim_angle), offsetof(stdb_shot, club_id),
                                 offsetof(stdb_shot, power), offsetof(stdb_shot, cigarette_active),
                                 offsetof(stdb_shot, wind_time)});
    describe(sizeof(stdb_shot_event),
             {offsetof(stdb_shot_event, room_id), offsetof(stdb_shot_event, account_id), offsetof(stdb_shot_event, zone),
              offsetof(stdb_shot_event, shot), offsetof(stdb_shot_event, start_x), offsetof(stdb_shot_event, start_y),
              offsetof(stdb_shot_event, start_z), offsetof(stdb_shot_event, rest_x), offsetof(stdb_shot_event, rest_y),
              offsetof(stdb_shot_event, rest_z), offsetof(stdb_shot_event, holed)});
    describe(sizeof(stdb_emote_event), {offsetof(stdb_emote_event, room_id), offsetof(stdb_emote_event, account_id),
                                        offsetof(stdb_emote_event, emote_id)});
    describe(sizeof(stdb_link_code), {offsetof(stdb_link_code, code), offsetof(stdb_link_code, expires_at_micros)});
    describe(sizeof(stdb_link_status), {offsetof(stdb_link_status, result), offsetof(stdb_link_status, at_micros)});
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

net_client::net_client(stdb_client* client) : client_(client) {}

std::optional<net_client> net_client::create(const online_config& config,
                                             const std::string& page_signed_in,
                                             const std::string& page_failed) {
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
    return net_client(client);
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
    stdb_claim_name(client_.get(), name.data(), name.size());
}

void net_client::join_course(const std::string& course_id) {
    stdb_join_course(client_.get(), course_id.data(), course_id.size());
}

void net_client::leave_room() {
    stdb_leave_room(client_.get());
}

void net_client::create_group() {
    stdb_create_group(client_.get());
}

void net_client::join_group(const std::uint64_t group_id) {
    stdb_join_group(client_.get(), group_id);
}

void net_client::leave_group() {
    stdb_leave_group(client_.get());
}

void net_client::return_to_hub() {
    stdb_return_to_hub(client_.get());
}

void net_client::create_link_code() {
    stdb_create_link_code(client_.get());
}

void net_client::redeem_link_code(const std::string& code) {
    stdb_redeem_link_code(client_.get(), code.data(), code.size());
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
    send_commands(state);
    stdb_frame_tick(client_.get());

    std::array<stdb_event, 64> events{};
    std::size_t count = 0;
    while ((count = stdb_poll(client_.get(), events.data(), events.size())) > 0) {
        for (std::size_t i = 0; i < count; ++i) {
            handle(events[i], state);
        }
    }

    deliver_progress(state);
    room_.publish(state);
    state.online.status = status_;
    state.online.server_now = clock_.now(local_now());
}

void net_client::deliver_progress(game_state& state) {
    if (progress_changed_ && self_applied_) {
        receive_online_progress(state, progress());
        progress_changed_ = false;
    }
}

void net_client::send_commands(game_state& state) {
    if (status_ != net_status::connected) {
        state.net_commands.clear();  // nothing to send them to; the server's state wins on reconnect
        return;
    }
    stdb_client* client = client_.get();
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    for (const net_command& command : state.net_commands) {
        switch (command.type) {
        case net_command_type::motion: {
            const net_motion& m = command.motion;
            const stdb_motion motion{m.zone, static_cast<std::uint8_t>(m.mode), m.position.x, m.position.y, m.position.z,
                                     m.yaw, m.speed, m.turn_rate, now};
            stdb_update_motion(client, &motion);
            break;
        }
        case net_command_type::take_shot: {
            const shot_input& s = command.shot;
            const stdb_shot shot{command.stroke, s.aim_angle, view(s.club_id), s.power, s.cigarette_active, s.wind_time};
            stdb_take_shot(client, &shot);
            break;
        }
        case net_command_type::enter_hole:
            stdb_enter_hole(client, static_cast<std::int32_t>(command.hole_index));
            break;
        case net_command_type::claim_collectible:
            stdb_claim_collectible(client, command.collectible_id.data(), command.collectible_id.size());
            break;
        case net_command_type::emote: {
            const std::string name = emote_name(command.emote);
            stdb_emote(client, name.data(), name.size());
            break;
        }
        case net_command_type::retee:
            stdb_retee(client);
            break;
        case net_command_type::pick_up_ball:
            stdb_pick_up_ball(client);
            break;
        case net_command_type::create_group:
            stdb_create_group(client);
            break;
        case net_command_type::join_group:
            stdb_join_group(client, command.group_id);
            break;
        case net_command_type::leave_group:
            stdb_leave_group(client);
            break;
        case net_command_type::return_to_hub:
            stdb_return_to_hub(client);
            break;
        }
    }
    state.net_commands.clear();
}

void net_client::handle(const stdb_event& event, game_state& state) {
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
        forget_session(state);
        status_ = net_status::connected;
        subscribe(client_.get(), {"SELECT * FROM my_account", "SELECT * FROM my_link_code", "SELECT * FROM my_link_status"});
        break;
    case STDB_EVENT_DISCONNECTED:
        forget_session(state);
        status_ = event.retrying ? net_status::signing_in : net_status::signed_out;
        failure_ = text_of(event.text);
        break;
    case STDB_EVENT_SUBSCRIPTION_APPLIED:
        if (event.subscription == self_subscription_) {
            self_applied_ = true;
            progress_changed_ = true;
        }
        break;
    case STDB_EVENT_SUBSCRIPTION_FAILED:
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
        if (failure.reducer == "claim_collectible") {
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
    switch (event.table) {
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
    case STDB_TABLE_PLAYER:
        if (row.player.account_id == account_id_ && change != STDB_ROW_DELETE) {
            holes_completed_ = row.player.holes_completed;
            progress_changed_ = true;
        }
        room_.apply(event, account_id_, local_now(), clock_, state);
        break;
    case STDB_TABLE_PLAYER_SKILL:
        if (row.player_skill.account_id == account_id_) {
            apply_change(skills_, row.player_skill.id, change, {text_of(row.player_skill.skill_id), row.player_skill.xp});
            progress_changed_ = true;
        }
        break;
    case STDB_TABLE_COMPLETED_COURSE:
        if (row.completed_course.account_id == account_id_) {
            apply_change(completed_courses_, row.completed_course.id, change, text_of(row.completed_course.course_id));
            progress_changed_ = true;
        }
        break;
    case STDB_TABLE_COLLECTED:
        if (row.collected.account_id == account_id_) {
            const stdb_collected& c = row.collected;
            apply_change(collected_, c.id, change,
                         collected_state{text_of(c.collectible_id), c.repeatable, c.claim_count, c.claimed_at_holes_completed});
            progress_changed_ = true;
        }
        break;
    case STDB_TABLE_WORLD_FLAG:
        if (row.world_flag.account_id == account_id_) {
            apply_change(world_flags_, row.world_flag.id, change, text_of(row.world_flag.flag));
            progress_changed_ = true;
        }
        break;
    case STDB_TABLE_ROOM_MEMBER:
        if (row.room_member.account_id == account_id_) {
            const std::uint64_t room = change == STDB_ROW_DELETE ? 0 : row.room_member.room_id;
            if (room != room_id_) {
                state.online.room.reset();
                room_.clear(state);
                subscribe_room(room);
            }
        }
        room_.apply(event, account_id_, local_now(), clock_, state);
        break;
    case STDB_TABLE_ROOM:
        if (row.room.room_id == room_id_ && room_id_ != 0) {
            if (change == STDB_ROW_DELETE) {
                state.online.room.reset();
            } else {
                state.online.room = online_room{room_id_, text_of(row.room.course_id), row.room.player_count};
            }
        }
        break;
    default:
        room_.apply(event, account_id_, local_now(), clock_, state);
        break;
    }
}

// Not hole_score: it keeps every hole the account ever scored, and nothing
// shows it yet, so sending it would only grow every connection's traffic.
void net_client::subscribe_self() {
    self_applied_ = false;
    const std::string id = std::to_string(account_id_);
    self_subscription_ = subscribe(client_.get(), {
                                                      "SELECT * FROM player WHERE account_id = " + id,
                                                      "SELECT * FROM player_skill WHERE account_id = " + id,
                                                      "SELECT * FROM completed_course WHERE account_id = " + id,
                                                      "SELECT * FROM collected WHERE account_id = " + id,
                                                      "SELECT * FROM world_flag WHERE account_id = " + id,
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
            "SELECT * FROM avatar_motion WHERE room_id = " + id,
            "SELECT * FROM ball WHERE room_id = " + id,
            "SELECT * FROM shot_event WHERE room_id = " + id,
            "SELECT * FROM emote_event WHERE room_id = " + id,
            // Room members' names.
            "SELECT player.* FROM player JOIN room_member ON player.account_id = room_member.account_id WHERE room_member.room_id = " + id,
        });
}

void net_client::forget_session(game_state& state) {
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
    self_applied_ = false;
    progress_changed_ = false;
    holes_completed_ = 0;
    skills_.clear();
    completed_courses_.clear();
    collected_.clear();
    world_flags_.clear();
    room_.clear(state);
    state.online = online_view{};
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
