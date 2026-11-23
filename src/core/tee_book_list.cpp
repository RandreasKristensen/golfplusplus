#include "core/tee_book_list.h"
#include "game/mode_dispatch.h"
#include "game/text_ids.h"

namespace {
constexpr double micros_per_second = 1000000.0;

std::int64_t micros(const float seconds) { return static_cast<std::int64_t>(static_cast<double>(seconds) * micros_per_second); }

void fail(tee_list_state& list, std::string text_key) {
    list.phase = list_phase::failed;
    list.error = std::move(text_key);
}
}

void request_list(tee_list_state& list, game_state& game, const std::string& course) {
    const online_view& online = game.online;
    list = tee_list_state{list_phase::loading, course, online.book_answers, online.book_refusals, online.server_now, online.server_now, {}};
    net_command command;
    command.type = net_command_type::open_tee_book;
    command.course_id = course;
    if (!request_tee_action(game, command)) {
        fail(list, online_error_text_key(net_failure_not_connected));
    }
}

void update_list(tee_list_state& list, game_state& game, const bool refresh) {
    const online_view& online = game.online;
    const std::int64_t now = online.server_now;
    if (list.phase == list_phase::loading) {
        if (online.book_refusals > list.refusals_before) {
            fail(list, online_error_text_key(online.book_error));
        } else if (online.book_answers > list.answers_before && online.book_ready && online.book_course_id == list.course) {
            list.phase = list_phase::idle;
            list.error.clear();
            list.refreshed_at = now;
        } else if (list.requested_at == 0) {
            list.requested_at = now;  // the clock was not known when it was sent: the wait starts with it
        } else if (now - list.requested_at >= micros(game.tuning.net.tee_book_list_timeout_seconds)) {
            fail(list, text_hub_list_timeout);
        }
        return;
    }
    if (list.course.empty()) {
        return;
    }
    // A failed request is not asked again on its own: only the player retries.
    const bool due = list.phase == list_phase::idle && now - list.refreshed_at >= micros(game.tuning.net.tee_book_refresh_seconds);
    if (refresh || due) {
        request_list(list, game, list.course);
    }
}

counts_state counts_of(const tee_list_state& list, const game_state& game, const std::string& course) {
    const online_view& online = game.online;
    if (!online.book_ready || online.book_course_id != course) {
        return counts_state::unknown;
    }
    const bool asked = list.course == course;
    if (!online.book_live || (asked && list.phase == list_phase::failed)) {
        return counts_state::old;
    }
    return asked && list.phase == list_phase::loading ? counts_state::updating : counts_state::confirmed;
}
