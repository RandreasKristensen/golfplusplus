#include "core/tee_book.h"
#include "core/menu_controls.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/tee_times.h"
#include "game/text_ids.h"
#include <algorithm>
#include <map>

namespace {
enum class row_kind { play_now, course, slot, heading, courses, cancel, transport, return_hub };
// A slot's `count` is its booked golfers, or count_unknown until the server has confirmed
// counts for its course; a heading's, the days ahead of today it names.
struct book_row { row_kind kind; std::string course; std::int64_t time = 0; int count = 0; };
constexpr int count_unknown = -1;

// The book is showing courses to choose from, not one course's times.
bool listing_courses(const tee_book_state& menu, const game_state& game) {
    return game.hub_flow.screen == hub_screen::book && (!is_online(game) || menu.course.empty());
}
bool has_carts(const startup_catalog& catalog, const std::string& id) {
    const course_definition* course = catalog_course(catalog, id);
    return course != nullptr && course->carts;
}
// The choice of cart or carry as shown: my booking's where it could have a cart, else what I picked.
bool transport_choice(const tee_book_state& menu, const game_state& game, const startup_catalog& catalog) {
    const auto& booking = active_booking(game);
    return booking && has_carts(catalog, booking->course_id) ? booking->takes_cart : menu.takes_cart;
}
std::vector<book_row> rows(const tee_book_state& menu, const game_state& game, const startup_catalog& catalog) {
    if (game.hub_flow.screen == hub_screen::bus) { return {{row_kind::return_hub, {}}}; }
    std::vector<book_row> result;
    if (!is_online(game)) { result.push_back({row_kind::transport, {}}); }
    if (!is_online(game) || menu.course.empty()) {
        for (const auto& course : catalog.courses) {
            result.push_back({is_online(game) ? row_kind::course : row_kind::play_now, course.course.id});
        }
    } else {
        result.push_back({row_kind::courses, {}});
    }
    if (active_booking(game)) { result.push_back({row_kind::cancel, {}}); }
    // A course without carts offers only carry: no choice to make.
    if (is_online(game) && !menu.course.empty() && has_carts(catalog, menu.course)) { result.push_back({row_kind::transport, {}}); }
    // Every time comes from the clock, at once; the server has rows for the booked ones only,
    // and says which only once it has answered.
    if (!menu.course.empty() && is_online(game) && game.online.server_now > 0) {
        const bool counted = counts_of(menu.list, game, menu.course) != counts_state::unknown;
        std::map<std::int64_t, int> booked;
        if (counted) {
            for (const auto& [id, slot] : game.online.tee_slots) {
                if (slot.course_id == menu.course) { booked[slot.scheduled_at] = slot.booked_count; }
            }
        }
        const hub_clock clock = tee_book_clock(game);
        const std::int64_t today = hub_day_at(clock, game.online.server_now);
        std::int64_t listed_day = today - 1;
        for (const auto at : tee_times(game.online.server_now, game.tuning.server, clock)) {
            const std::int64_t day = hub_day_at(clock, at);
            if (day != listed_day) { result.push_back({row_kind::heading, menu.course, at, static_cast<int>(day - today)}); listed_day = day; }
            const auto found = booked.find(at);
            result.push_back({row_kind::slot, menu.course, at, !counted ? count_unknown : found != booked.end() ? found->second : 0});
        }
    }
    return result;
}
// The row selected: the chosen time where the list has moved under it (the horizon advances with the
// clock), the earliest time still listed if that one has passed, else the plain index.
int selected_row(const tee_book_state& menu, const std::vector<book_row>& choices) {
    if (menu.selected_time == 0) { return menu.selection; }
    int earliest = -1;
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (choices[i].kind != row_kind::slot) { continue; }
        if (choices[i].time == menu.selected_time) { return static_cast<int>(i); }
        if (earliest < 0) { earliest = static_cast<int>(i); }
    }
    return earliest >= 0 ? earliest : menu.selection;
}
// What a booking asks for: the course, its time (0 offline) and cart or carry.
struct book_request { std::string course; std::int64_t time = 0; bool takes_cart = false; };
// Single flight: a booking or cancellation is not sent while another waits for its reservation row.
void send(tee_book_state& menu, game_state& game, const net_command_type type, const book_request& request) {
    if (menu.pending) { return; }
    menu.pending = type; menu.scheduled = request.time; menu.pending_course = request.course; menu.pending_cart = request.takes_cart;
    menu.message.clear();
    net_command command; command.type = type; command.course_id = request.course; command.scheduled_at = request.time;
    command.takes_cart = request.takes_cart;
    request_tee_action(game, command);
}
// The count a slot shows, by how far the counts can be trusted.
std::string count_text(const counts_state counts, const int count, const game_state& game, const text_assets& text) {
    const std::string capacity = std::to_string(game.tuning.server.group_capacity);
    if (counts == counts_state::unknown || count == count_unknown) {
        return format_text(text, text_hub_count_unknown, {{"capacity", capacity}});
    }
    const char* key = counts == counts_state::confirmed ? text_hub_count : counts == counts_state::updating ? text_hub_count_updating : text_hub_count_old;
    return format_text(text, key, {{"count", std::to_string(count)}, {"capacity", capacity}});
}
// The one message under the title, by priority: an error to act on, then a failed list's reason and
// how to retry, then what is only progress.
struct shown_message { std::string text; message_severity severity = message_severity::status; };
shown_message book_message(const tee_book_state& menu, const text_assets& text) {
    if (!menu.message.empty()) { return {lookup_text(text, menu.message.c_str()), menu.severity}; }
    if (menu.list.phase == list_phase::failed) {
        return {format_text(text, text_hub_list_failed, {{"reason", lookup_text(text, menu.list.error.c_str())}}), message_severity::error};
    }
    if (menu.pending) { return {lookup_text(text, text_hub_pending), message_severity::status}; }
    if (menu.list.phase == list_phase::loading) { return {lookup_text(text, text_hub_list_pending), message_severity::status}; }
    return {};
}
}

void set_book_error(tee_book_state& menu, const std::string& key) {
    menu.message = key;
    menu.severity = message_severity::error;
}

std::vector<ui_sound> update_tee_book(tee_book_state& menu, game_state& game, const startup_catalog& catalog,
    const input_state& input, const menu_pointer& pointer, const std::vector<reducer_failure>& refusals) {
    std::vector<ui_sound> sounds;
    for (const auto& failure : refusals) {
        if (failure.reducer == "return_to_hub") {
            game.hub_flow.travel_course.clear(); game.hub_flow.screen = hub_screen::bus;
        } else if (failure.reducer != "book_tee" && failure.reducer != "cancel_tee") { continue; }
        menu.pending.reset(); set_book_error(menu, online_error_text_key(failure.error));
    }
    if (menu.screen != game.hub_flow.screen) {
        const auto message = menu.message;
        const auto severity = menu.severity;
        menu = tee_book_state{}; menu.screen = game.hub_flow.screen; menu.message = message; menu.severity = severity;
        return sounds; // F and clicks that opened the screen cannot also activate it
    }
    if (menu.screen == hub_screen::none) { return sounds; }
    const auto& booking = active_booking(game);
    if (menu.pending) {
        const bool booked = *menu.pending == net_command_type::book_tee && booking && booking->course_id == menu.pending_course &&
            booking->scheduled_at == menu.scheduled && booking->takes_cart == menu.pending_cart;
        const bool cancelled = *menu.pending == net_command_type::cancel_tee && !booking;
        if (booked || cancelled) { menu.pending.reset(); menu.message.clear(); }
    }
    if (input.escape.pressed) {
        // A transaction already sent may still succeed; leaving the screen never cancels it.
        game.hub_flow.screen = hub_screen::none; menu = tee_book_state{}; return {ui_sound::back};
    }
    // Listing counts never holds the menu: it follows its own request, whatever else is under way.
    if (menu.screen == hub_screen::book && is_online(game) && !menu.course.empty()) { update_list(menu.list, game, input.key_r.pressed); }
    const auto choices = rows(menu, game, catalog);
    menu.selection = selected_row(menu, choices);
    std::vector<bool> inert;
    for (const auto& choice : choices) { inert.push_back(choice.kind == row_kind::heading); }
    const auto screen = listing_courses(menu, game) ? startup_menu_screen::course_book : startup_menu_screen::tee_book;
    const int hit = menu.screen == hub_screen::book ? select_course(menu.selection, menu.top, static_cast<int>(choices.size()), input, pointer, sounds, screen, inert) :
        select_with_input(startup_menu_screen::confirm, 1, menu.selection, static_cast<int>(choices.size()), input, pointer, sounds);
    const bool chosen = !choices.empty() && menu.selection < static_cast<int>(choices.size());
    menu.selected_time = chosen && choices[static_cast<std::size_t>(menu.selection)].kind == row_kind::slot ? choices[static_cast<std::size_t>(menu.selection)].time : 0;
    if (!(input.enter.pressed || input.space.pressed || hit >= 0) || choices.empty()) { return sounds; }
    sounds.push_back(ui_sound::select);
    const auto& choice = choices[static_cast<std::size_t>(menu.selection)];
    switch (choice.kind) {
    case row_kind::play_now:
        menu.course = choice.course;
        send(menu, game, net_command_type::book_tee, {choice.course, 0, transport_choice(menu, game, catalog) && has_carts(catalog, choice.course)});
        break;
    case row_kind::course:
        menu.course = choice.course; menu.selection = menu.top = 0; menu.selected_time = 0; menu.message.clear();
        request_list(menu.list, game, menu.course); break;
    case row_kind::slot:
        if (booking && booking->course_id == choice.course && booking->scheduled_at == choice.time) { break; }
        if (choice.time <= game.online.server_now) { set_book_error(menu, text_hub_slot_elapsed); }
        // Only counts the server still confirms refuse a booking here; otherwise it decides, and a refusal leaves my booking as it is.
        else if (counts_of(menu.list, game, choice.course) == counts_state::confirmed && choice.count >= game.tuning.server.group_capacity) { set_book_error(menu, text_hub_slot_full); }
        else { send(menu, game, net_command_type::book_tee, {choice.course, choice.time, transport_choice(menu, game, catalog) && has_carts(catalog, choice.course)}); }
        break;
    case row_kind::courses:
        menu.course.clear(); menu.selection = menu.top = 0; menu.selected_time = 0; menu.message.clear(); menu.list = tee_list_state{}; break;
    case row_kind::cancel: send(menu, game, net_command_type::cancel_tee, {menu.course}); break;
    case row_kind::transport: {
        const bool next = !transport_choice(menu, game, catalog);
        // Changing a booking's choice books the same time again; the book checks it like any replacement.
        if (booking && has_carts(catalog, booking->course_id)) {
            send(menu, game, net_command_type::book_tee, {booking->course_id, booking->scheduled_at, next});
        } else { menu.takes_cart = next; }
        break;
    }
    case row_kind::return_hub: request_bus_travel(game); break;
    case row_kind::heading: break;
    }
    return sounds;
}
render_startup_menu render_tee_book(const tee_book_state& menu, const game_state& game,
    const startup_catalog& catalog, const text_assets& text) {
    render_startup_menu data;
    const bool book = game.hub_flow.screen == hub_screen::book;
    data.screen = !book ? startup_menu_screen::confirm : listing_courses(menu, game) ? startup_menu_screen::course_book : startup_menu_screen::tee_book;
    data.title = lookup_text(text, book ? text_hub_book_title : text_hub_bus_title);
    data.subtitle = booking_text(game, catalog, text);
    if (const shown_message message = book_message(menu, text); !message.text.empty()) { set_message(data, message.text, message.severity); }
    const auto choices = rows(menu, game, catalog);
    const int selected = selected_row(menu, choices);
    const int per_page = startup_picker_columns(data.screen) * startup_picker_rows(data.screen);
    const int first = book ? menu.top * startup_picker_columns(data.screen) : 0;
    const int end = std::min(static_cast<int>(choices.size()), first + (book ? per_page : static_cast<int>(choices.size())));
    data.more_above = first > 0; data.more_below = end < static_cast<int>(choices.size());
    for (int i = first; i < end; ++i) {
        const auto& row = choices[static_cast<std::size_t>(i)];
        std::string title, hint, figures;
        std::optional<render_hole_preview> preview;
        const char* key = nullptr;
        switch (row.kind) {
        case row_kind::play_now:
        case row_kind::course:
            for (const startup_course_option& option : catalog.courses) {
                if (option.course.id != row.course) { continue; }
                title = option.course.name;
                figures = course_figures_text(option, text);
                hint = course_detail_text(option, game, text, shared_unix_ms(game, game.local_unix_ms));
                preview = option.preview;
            }
            break;
        case row_kind::heading:
            add_heading(data, lookup_text(text, row.count == 0 ? text_hub_book_today : text_hub_book_tomorrow));
            continue;
        case row_kind::slot: {
            title = tee_clock_text(row.time, game, text);
            const std::string counted = count_text(counts_of(menu.list, game, row.course), row.count, game, text);
            hint = counted;
            if (const course_definition* course = catalog_course(catalog, row.course)) {
                hint = format_text(text, text_hub_slot_there, {{"count", counted}, {"there", course_time_text(*course, game, text, row.time / 1000)}});
            }
            if (row.time <= game.online.server_now) { hint = lookup_text(text, text_hub_slot_elapsed); }
            else if (active_booking(game) && active_booking(game)->scheduled_at == row.time && active_booking(game)->course_id == row.course) { hint = format_text(text, text_hub_own_slot, {{"count", counted}}); }
            break;
        }
        case row_kind::courses: key = text_hub_courses; break;
        case row_kind::cancel: key = text_hub_cancel; break;
        case row_kind::transport:
            title = format_text(text, text_hub_transport, {{"choice", lookup_text(text, transport_choice(menu, game, catalog) ? text_menu_transport_cart : text_menu_transport_carry)}});
            hint = lookup_text(text, text_hub_transport_hint);
            break;
        case row_kind::return_hub: key = text_hub_return; break;
        }
        if (key != nullptr && title.empty()) { title = lookup_text(text, key); }
        else if (key != nullptr && hint.empty()) { hint = lookup_text(text, key); }
        add_tile(data, title, hint, i == selected);
        data.tiles.back().figures = figures;
        data.tiles.back().preview = preview;
    }
    return data;
}
