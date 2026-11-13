#include "core/tee_book.h"
#include "core/menu_controls.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "core/vcr_frame.h"
#include "game/sky_clock.h"
#include "game/tee_times.h"
#include "game/text_ids.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace {
enum class row_kind { play_now, course, slot, refresh, courses, cancel, close, cart, carry, return_hub };
struct book_row { row_kind kind; std::string course; std::int64_t time = 0; int count = 0; };
std::vector<book_row> rows(const tee_book_state& menu, const game_state& game, const startup_catalog& catalog) {
    if (game.hub_flow.screen == hub_screen::bus) {
        if (game.place.id != online_hub_course) { return {{row_kind::return_hub, {}}, {row_kind::close, {}}}; }
        const auto booking = active_booking(game);
        const auto course = std::find_if(catalog.courses.begin(), catalog.courses.end(), [&](const auto& c) {
            return booking && c.course.id == booking->course_id;
        });
        if (course != catalog.courses.end() && course->course.carts) {
            return {{row_kind::cart, {}}, {row_kind::carry, {}}, {row_kind::close, {}}};
        }
        return {{row_kind::carry, {}}, {row_kind::close, {}}};
    }
    std::vector<book_row> result;
    if (!is_online(game) || menu.course.empty()) {
        for (const auto& course : catalog.courses) {
            result.push_back({is_online(game) ? row_kind::course : row_kind::play_now, course.course.id});
        }
    } else {
        result.push_back({row_kind::refresh, {}});
        result.push_back({row_kind::courses, {}});
    }
    if (active_booking(game)) { result.push_back({row_kind::cancel, {}}); }
    result.push_back({row_kind::close, {}});
    if (!menu.course.empty() && is_online(game) && game.online.book_ready && game.online.book_course_id == menu.course) {
        // Every time from the clock; the server has rows for the booked ones only.
        std::map<std::int64_t, int> booked;
        for (const auto& [id, slot] : game.online.tee_slots) {
            if (slot.course_id == menu.course) { booked[slot.scheduled_at] = slot.booked_count; }
        }
        for (const auto at : tee_times(game.online.server_now, game.tuning.server)) {
            const auto found = booked.find(at);
            result.push_back({row_kind::slot, menu.course, at, found != booked.end() ? found->second : 0});
        }
    }
    return result;
}
void send(tee_book_state& menu, game_state& game, const net_command_type type, const std::int64_t time = 0) {
    if (menu.pending) { return; }
    menu.pending = type; menu.scheduled = time; menu.message.clear();
    net_command command; command.type = type; command.course_id = menu.course; command.scheduled_at = time;
    if (type == net_command_type::open_tee_book) {
        game.online.book_ready = false;
        // A refreshed horizon may remove elapsed rows: never activate a different
        // time at the previous row index with the next Enter press.
        menu.selection = menu.top = 0;
    }
    request_tee_action(game, command);
}
std::string course_name(const startup_catalog& catalog, const std::string& id) {
    for (const auto& c : catalog.courses) { if (c.course.id == id) { return c.course.name; } }
    return id;
}
}
std::string tee_time_text(const std::int64_t micros, const game_state& game, const text_assets& text) {
    const double offset = static_cast<double>(game.hub_sky.utc_offset_hours);
    const double at = game_day_at(game.tuning.sky_clock, micros / 1000);
    const double now = game_day_at(game.tuning.sky_clock, game.online.server_now / 1000);
    const std::string clock = make_datestamp(text, at, offset).time;
    const auto days = static_cast<long long>(std::floor(at + offset / 24.0) - std::floor(now + offset / 24.0));
    return days <= 0 ? clock : format_text(text, text_hub_time_later_day, {{"time", clock}, {"days", std::to_string(days)}});
}
std::string booking_text(const game_state& game, const startup_catalog& catalog, const text_assets& text) {
    const auto& booking = active_booking(game);
    if (!booking) { return lookup_text(text, text_hub_no_booking); }
    if (booking->scheduled_at == 0) { return course_name(catalog, booking->course_id); }
    const char* key = booking->completed ? text_hub_completed : reservation_expired(game) ? text_hub_expired : booking->started_at != 0 ? text_hub_started : text_hub_booking;
    return format_text(text, key, {{"actual", tee_time_text(booking->started_at, game, text)}, {"course", course_name(catalog, booking->course_id)}, {"time", tee_time_text(booking->scheduled_at, game, text)},
        {"expiry", tee_time_text(booking->scheduled_at + game.tuning.server.tee_expiry_minutes * minute_micros, game, text)}});
}
std::vector<std::string> prop_board_labels(const game_state& game, const startup_catalog& catalog, const text_assets& text) {
    std::vector<std::string> labels;
    for (const auto& prop : game.area.props) {
        const auto& booking = active_booking(game);
        labels.push_back(prop.model == "tee_book" ? lookup_text(text, text_hub_book_title)
            : prop.model != "hub_bus" ? std::string()
            : game.kind != place_kind::hub ? lookup_text(text, text_hub_bus_board_hub)
            : booking ? course_name(catalog, booking->course_id) : lookup_text(text, text_hub_bus_title));
    }
    return labels;
}
std::vector<ui_sound> update_tee_book(tee_book_state& menu, game_state& game, const startup_catalog& catalog,
    const input_state& input, const menu_pointer& pointer, const std::vector<reducer_failure>& refusals) {
    std::vector<ui_sound> sounds;
    for (const auto& failure : refusals) {
        if (failure.reducer == "take_bus" || failure.reducer == "return_to_hub") {
            game.hub_flow.travel_course.clear(); game.hub_flow.screen = hub_screen::bus;
        } else if (failure.reducer != "open_tee_book" && failure.reducer != "book_tee" && failure.reducer != "cancel_tee") { continue; }
        menu.pending.reset(); menu.message = online_error_text_key(failure.error);
        menu.refreshed_at = game.online.server_now;
    }
    if (menu.screen != game.hub_flow.screen) {
        const auto message = menu.message;
        menu = tee_book_state{}; menu.screen = game.hub_flow.screen; menu.message = message;
        return sounds; // F and clicks that opened the screen cannot also activate it
    }
    if (menu.screen == hub_screen::none) { return sounds; }
    const auto& booking = active_booking(game);
    if (menu.pending) {
        const bool listed = *menu.pending == net_command_type::open_tee_book && game.online.book_ready && game.online.book_course_id == menu.course;
        const bool booked = *menu.pending == net_command_type::book_tee && booking && booking->course_id == menu.course && booking->scheduled_at == menu.scheduled;
        const bool cancelled = *menu.pending == net_command_type::cancel_tee && !booking;
        if (listed || booked || cancelled) {
            menu.pending.reset(); menu.message.clear();
            if (listed) { menu.refreshed_at = game.online.server_now; }
        }
    }
    if (input.escape.pressed) {
        // A transaction already sent may still succeed; leaving the screen never cancels it.
        game.hub_flow.screen = hub_screen::none; menu = tee_book_state{}; return {ui_sound::back};
    }
    if (menu.pending) { return sounds; }
    if (!menu.course.empty() && is_online(game) && (input.key_r.pressed || game.online.server_now - menu.refreshed_at >= 60LL * 1000000LL)) {
        send(menu, game, net_command_type::open_tee_book); return sounds;
    }
    const auto choices = rows(menu, game, catalog);
    const auto screen = menu.screen == hub_screen::book ? startup_menu_screen::tee_book : startup_menu_screen::confirm;
    const int hit = menu.screen == hub_screen::book ? select_course(menu.selection, menu.top, static_cast<int>(choices.size()), input, pointer, sounds, startup_menu_screen::tee_book) :
        select_with_input(screen, 1, menu.selection, static_cast<int>(choices.size()), input, pointer, sounds);
    if (!(input.enter.pressed || input.space.pressed || hit >= 0) || choices.empty()) { return sounds; }
    sounds.push_back(ui_sound::select);
    const auto& choice = choices[static_cast<std::size_t>(menu.selection)];
    switch (choice.kind) {
    case row_kind::play_now:
        menu.course = choice.course;
        send(menu, game, net_command_type::book_tee);
        break;
    case row_kind::course:
        menu.course = choice.course; menu.selection = menu.top = 0;
        send(menu, game, net_command_type::open_tee_book); break;
    case row_kind::slot:
        if (booking && booking->course_id == choice.course && booking->scheduled_at == choice.time) { break; }
        if (choice.time <= game.online.server_now) { menu.message = text_hub_slot_elapsed; }
        else if (choice.count >= game.tuning.server.group_capacity) { menu.message = text_hub_slot_full; }
        else { send(menu, game, net_command_type::book_tee, choice.time); }
        break;
    case row_kind::refresh: send(menu, game, net_command_type::open_tee_book); break;
    case row_kind::courses: menu.course.clear(); menu.selection = menu.top = 0; menu.message.clear(); break;
    case row_kind::cancel: send(menu, game, net_command_type::cancel_tee); break;
    case row_kind::close: game.hub_flow.screen = hub_screen::none; menu = tee_book_state{}; break;
    case row_kind::cart:
    case row_kind::carry: request_bus_travel(game, choice.kind == row_kind::cart); break;
    case row_kind::return_hub: request_bus_travel(game, false); break;
    }
    return sounds;
}
render_startup_menu render_tee_book(const tee_book_state& menu, const game_state& game,
    const startup_catalog& catalog, const text_assets& text) {
    render_startup_menu data;
    const bool book = game.hub_flow.screen == hub_screen::book;
    data.screen = book ? startup_menu_screen::tee_book : startup_menu_screen::confirm;
    data.title = lookup_text(text, book ? text_hub_book_title : text_hub_bus_title);
    data.subtitle = booking_text(game, catalog, text);
    data.footer = is_online(game) ? format_text(text, text_hub_controls, {{"now", tee_time_text(game.online.server_now, game, text)}}) : lookup_text(text, text_hub_offline_controls);
    if (!menu.message.empty()) { data.message = lookup_text(text, menu.message.c_str()); }
    if (menu.pending) { data.message = lookup_text(text, text_hub_pending); }
    const auto choices = rows(menu, game, catalog);
    const int first = book ? menu.top * picker_columns : 0;
    const int end = std::min(static_cast<int>(choices.size()), first + (book ? picker_rows * picker_columns : static_cast<int>(choices.size())));
    data.more_above = first > 0; data.more_below = end < static_cast<int>(choices.size());
    for (int i = first; i < end; ++i) {
        const auto& row = choices[static_cast<std::size_t>(i)];
        std::string title, hint;
        const char* key = text_hub_back;
        switch (row.kind) {
        case row_kind::play_now: title = course_name(catalog, row.course); key = text_hub_play_now; break;
        case row_kind::course: title = course_name(catalog, row.course); key = text_hub_choose_course; break;
        case row_kind::slot:
            title = tee_time_text(row.time, game, text);
            hint = format_text(text, text_hub_count, {{"count", std::to_string(row.count)}, {"capacity", std::to_string(game.tuning.server.group_capacity)}});
            if (row.time <= game.online.server_now) { hint = lookup_text(text, text_hub_slot_elapsed); }
            else if (active_booking(game) && active_booking(game)->scheduled_at == row.time && active_booking(game)->course_id == row.course) { hint = format_text(text, text_hub_own_slot, {{"count", std::to_string(row.count)}, {"capacity", std::to_string(game.tuning.server.group_capacity)}}); }
            break;
        case row_kind::refresh: key = text_hub_refresh; break;
        case row_kind::courses: key = text_hub_courses; break;
        case row_kind::cancel: key = text_hub_cancel; break;
        case row_kind::cart: key = text_menu_transport_cart; hint = lookup_text(text, text_menu_transport_cart_hint); break;
        case row_kind::carry: key = text_menu_transport_carry; hint = lookup_text(text, text_menu_transport_carry_hint); break;
        case row_kind::close: break;
        case row_kind::return_hub: key = text_hub_return; break;
        }
        if (title.empty()) { title = lookup_text(text, key); }
        else if (hint.empty()) { hint = lookup_text(text, key); }
        add_tile(data, title, hint, i == menu.selection);
    }
    return data;
}
