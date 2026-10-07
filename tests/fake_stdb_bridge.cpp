#include "fake_stdb_bridge.h"

#include "net/net_client.h"

#include <utility>

struct stdb_client {};

namespace {
stdb_client the_client;

void record(const std::string& name, const std::string& text = {}, const std::int64_t number = 0) {
    fake_stdb_bridge::call call;
    call.name = name;
    call.text = text;
    call.number = number;
    fake_bridge().calls.push_back(std::move(call));
}

std::string text_of(const char* data, const std::size_t len) {
    return data != nullptr ? std::string(data, len) : std::string();
}
}

fake_stdb_bridge& fake_bridge() {
    static fake_stdb_bridge bridge;
    return bridge;
}

void fake_stdb_bridge::reset() {
    *this = fake_stdb_bridge{};
}

std::vector<std::string> fake_stdb_bridge::call_names() const {
    std::vector<std::string> names;
    for (const call& c : calls) {
        names.push_back(c.name);
    }
    return names;
}

online_config fake_online_config() {
    online_config config;
    config.server_uri = "http://localhost:3000";
    config.database = "golfpp";
    config.anonymous = true;
    return config;
}

stdb_event fake_event(const stdb_event_kind kind) {
    stdb_event event{};
    event.kind = kind;
    return event;
}

void queue_event(const stdb_event& event) {
    fake_bridge().events.push_back(event);
}

stdb_event row_event(const stdb_table table, const stdb_row_change change, const stdb_row& row) {
    stdb_event event{};
    event.kind = STDB_EVENT_ROW;
    event.table = table;
    event.change = change;
    event.row = row;
    return event;
}

stdb_string fake_text(const std::string& text) {
    const std::string& kept = fake_bridge().texts.emplace_back(text);
    return stdb_string{kept.data(), kept.size()};
}

extern "C" {
size_t stdb_layout(size_t* out, const size_t max) {
    std::vector<std::size_t> layout = stdb_header_layout();
    if (fake_bridge().wrong_layout) {
        layout[layout.size() / 2] += 4;
    }
    for (std::size_t i = 0; i < layout.size() && i < max && out != nullptr; ++i) {
        out[i] = layout[i];
    }
    return layout.size();
}

stdb_client* stdb_create(const stdb_config* config) {
    record("stdb_create", text_of(config->server_uri.data, config->server_uri.len), config->anonymous ? 1 : 0);
    return &the_client;
}

void stdb_destroy(stdb_client*) {
    record("stdb_destroy");
}

bool stdb_begin_login(stdb_client*, const bool silent_only) {
    record("stdb_begin_login", {}, silent_only ? 1 : 0);
    return !fake_bridge().busy;
}

void stdb_cancel_login(stdb_client*) {
    record("stdb_cancel_login");
}

void stdb_sign_out(stdb_client*) {
    record("stdb_sign_out");
}

int32_t stdb_frame_tick(stdb_client*) {
    return fake_bridge().connected ? 1 : 0;
}

size_t stdb_poll(stdb_client*, stdb_event* out, const size_t max) {
    std::size_t count = 0;
    std::deque<stdb_event>& events = fake_bridge().events;
    while (count < max && !events.empty()) {
        out[count++] = events.front();
        events.pop_front();
    }
    return count;
}

uint32_t stdb_subscribe(stdb_client*, const stdb_string* queries, const size_t count) {
    std::vector<std::string> sql;
    for (std::size_t i = 0; i < count; ++i) {
        sql.push_back(text_of(queries[i].data, queries[i].len));
    }
    fake_bridge().subscriptions.push_back(std::move(sql));
    return static_cast<uint32_t>(fake_bridge().subscriptions.size());
}

void stdb_unsubscribe(stdb_client*, const uint32_t subscription) {
    fake_bridge().unsubscribed.push_back(subscription);
}

void stdb_claim_name(stdb_client*, const char* name, const size_t len) {
    record("stdb_claim_name", text_of(name, len));
}

void stdb_join_course(stdb_client*, const char* course_id, const size_t len) {
    record("stdb_join_course", text_of(course_id, len));
}

void stdb_leave_room(stdb_client*) {
    record("stdb_leave_room");
}

void stdb_create_group(stdb_client*) {
    record("stdb_create_group");
}

void stdb_join_group(stdb_client*, const uint64_t group_id) {
    record("stdb_join_group", {}, static_cast<std::int64_t>(group_id));
}

void stdb_leave_group(stdb_client*) {
    record("stdb_leave_group");
}

void stdb_update_motion(stdb_client*, const stdb_motion* motion) {
    record("stdb_update_motion");
    fake_bridge().calls.back().motion = *motion;
}

void stdb_enter_hole(stdb_client*, const int32_t hole_index) {
    record("stdb_enter_hole", {}, hole_index);
}

void stdb_return_to_hub(stdb_client*) {
    record("stdb_return_to_hub");
}

void stdb_retee(stdb_client*) {
    record("stdb_retee");
}

void stdb_pick_up_ball(stdb_client*) {
    record("stdb_pick_up_ball");
}

void stdb_take_shot(stdb_client*, const stdb_shot* shot) {
    record("stdb_take_shot");
    fake_bridge().calls.back().shot = *shot;
    fake_bridge().calls.back().club_id = text_of(shot->club_id.data, shot->club_id.len);
}

void stdb_emote(stdb_client*, const char* emote_id, const size_t len) {
    record("stdb_emote", text_of(emote_id, len));
}

void stdb_claim_collectible(stdb_client*, const char* collectible_id, const size_t len) {
    record("stdb_claim_collectible", text_of(collectible_id, len));
}

void stdb_create_link_code(stdb_client*) {
    record("stdb_create_link_code");
}

void stdb_redeem_link_code(stdb_client*, const char* code, const size_t len) {
    record("stdb_redeem_link_code", text_of(code, len));
}
}
