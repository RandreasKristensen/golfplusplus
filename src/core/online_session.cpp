#include "core/online_session.h"

#include "game/text_ids.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace {
constexpr std::int64_t micros_per_minute = 60LL * 1000000LL;
}

online_session::online_session(net_client client) : net_(std::move(client)) {}

void online_session::update(game_state& state) {
    refusals_.clear();
    net_.update(state);
    while (std::optional<reducer_failure> failure = net_.take_reducer_failure()) {
        refusals_.push_back(std::move(*failure));
    }
}

online_menu_status online_session::menu_status(const game_state& state, const std::chrono::system_clock::time_point now) const {
    online_menu_status status;
    status.available = true;
    status.status = net_.status();
    if (!net_.failure().empty()) {
        status.failure_id = failure_id(net_.failure());
    }
    status.account_id = state.online.account_id;
    status.name = state.online.display_name;
    if (state.online.room) {
        status.room_course_id = state.online.room->course_id;
    }
    if (const std::optional<link_code_info>& code = net_.link_code()) {
        const std::int64_t now_micros =
            std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        const std::int64_t left = code->expires_at_micros - now_micros;
        if (left > 0) {
            status.link_code = code->code;
            // Rounded up: a code with seconds left still shows a minute.
            status.link_code_minutes_left = static_cast<int>((left + micros_per_minute - 1) / micros_per_minute);
        }
    }
    status.link_result = net_.link_result();
    status.link_results = net_.link_results();
    status.reducer_failures = refusals_;
    return status;
}

void online_session::carry_out(const std::vector<online_request>& requests, game_state& state) {
    for (const online_request& request : requests) {
        switch (request.type) {
        case online_request_type::begin_silent_login:
            net_.begin_silent_login();
            break;
        case online_request_type::begin_login:
            net_.begin_login();
            break;
        case online_request_type::cancel_login:
            net_.cancel_login();
            break;
        case online_request_type::sign_out:
            net_.sign_out(state);
            break;
        case online_request_type::claim_name:
            net_.claim_name(request.text);
            break;
        case online_request_type::redeem_link_code:
            net_.redeem_link_code(request.text);
            break;
        case online_request_type::create_link_code:
            net_.create_link_code();
            break;
        case online_request_type::join_course:
            net_.join_course(request.text);
            break;
        case online_request_type::leave_room:
            net_.leave_room();
            break;
        }
    }
}

online_round_status online_session::watch_round(const game_state& state) {
    const net_status status = net_.status();
    if (status == net_status::signed_out || status == net_status::failed) {
        rejoining_ = false;
        return {online_round_event::lost, {}};
    }
    if (status != net_status::connected) {
        // Signing in again by itself: the room is gone once it is back.
        rejoining_ = true;
        rejoin_sent_ = false;
        return {};
    }

    const std::optional<online_room>& room = state.online.room;
    const bool in_course_room = room && room->course_id == state.course.id;
    if (!rejoining_) {
        if (in_course_room) {
            return {};
        }
        // Taken out of the room while connected: this account signed in
        // somewhere else, which ends the room state here.
        return {online_round_event::lost, text_online_room_lost};
    }
    const bool refused = std::any_of(refusals_.begin(), refusals_.end(),
                                     [](const reducer_failure& failure) { return failure.reducer == "join_course"; });
    if (refused) {
        rejoining_ = false;
        return {online_round_event::lost, text_online_room_lost};
    }
    if (in_course_room && rejoin_sent_) {
        rejoining_ = false;
        return {online_round_event::restart, {}};
    }
    if (!rejoin_sent_ && state.online.account_id != 0) {
        net_.join_course(state.course.id);
        rejoin_sent_ = true;
    }
    return {};
}

void online_session::leave_round() {
    rejoining_ = false;
    net_.leave_room();
}
