#include "core/tee_book.h"
#include "core/departure_board.h"
#include "core/menu_controls.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/model_kinds.h"
#include "physics/vector_math.h"
#include "core/vcr_frame.h"
#include "game/sky_clock.h"
#include "game/tee_times.h"
#include "game/tree_season.h"
#include "game/text_ids.h"
#include <algorithm>
#include <cmath>
#include <map>

#include <glm/trigonometric.hpp>

namespace {
enum class row_kind { play_now, course, slot, heading, courses, cancel, transport, return_hub };
// A slot's `count` is its booked golfers; a heading's, the days ahead of today it names.
struct book_row { row_kind kind; std::string course; std::int64_t time = 0; int count = 0; };
hub_clock clock_of(const game_state& game) {
    return hub_clock{game.tuning.sky_clock, static_cast<double>(game.hub_sky.utc_offset_hours)};
}
// The book is showing courses to choose from, not one course's times.
bool listing_courses(const tee_book_state& menu, const game_state& game) {
    return game.hub_flow.screen == hub_screen::book && (!is_online(game) || menu.course.empty());
}
const course_definition* find_course(const startup_catalog& catalog, const std::string& id) {
    for (const auto& c : catalog.courses) { if (c.course.id == id) { return &c.course; } }
    return nullptr;
}
bool has_carts(const startup_catalog& catalog, const std::string& id) {
    const course_definition* course = find_course(catalog, id);
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
    if (!menu.course.empty() && is_online(game) && game.online.book_ready && game.online.book_course_id == menu.course) {
        // Every time from the clock; the server has rows for the booked ones only.
        std::map<std::int64_t, int> booked;
        for (const auto& [id, slot] : game.online.tee_slots) {
            if (slot.course_id == menu.course) { booked[slot.scheduled_at] = slot.booked_count; }
        }
        const hub_clock clock = clock_of(game);
        const std::int64_t today = hub_day_at(clock, game.online.server_now);
        std::int64_t listed_day = today - 1;
        for (const auto at : tee_times(game.online.server_now, game.tuning.server, clock)) {
            const std::int64_t day = hub_day_at(clock, at);
            if (day != listed_day) { result.push_back({row_kind::heading, menu.course, at, static_cast<int>(day - today)}); listed_day = day; }
            const auto found = booked.find(at);
            result.push_back({row_kind::slot, menu.course, at, found != booked.end() ? found->second : 0});
        }
    }
    return result;
}
// What a booking asks for: the course, its time (0 offline) and cart or carry.
struct book_request { std::string course; std::int64_t time = 0; bool takes_cart = false; };
void send(tee_book_state& menu, game_state& game, const net_command_type type, const book_request& request) {
    if (menu.pending) { return; }
    menu.pending = type; menu.scheduled = request.time; menu.pending_course = request.course; menu.pending_cart = request.takes_cart;
    menu.message.clear();
    net_command command; command.type = type; command.course_id = request.course; command.scheduled_at = request.time;
    command.takes_cart = request.takes_cart;
    if (type == net_command_type::open_tee_book) {
        game.online.book_ready = false;
        // A refreshed horizon may remove elapsed rows: never activate a different
        // time at the previous row index with the next Enter press.
        menu.selection = menu.top = 0;
    }
    request_tee_action(game, command);
}
}

std::string with_thousands(const int value) {
    std::string digits = std::to_string(value);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) { digits.insert(static_cast<std::size_t>(at), ","); }
    return digits;
}

std::string course_name(const startup_catalog& catalog, const std::string& id) {
    const course_definition* course = find_course(catalog, id);
    return course != nullptr ? course->name : id;
}

const char* part_of_day_key(const float sun_elevation_deg, const bool before_noon, const float solar_hours_from_noon) {
    // The sun's height (degrees) bounds of dawn's and dusk's twilight, the
    // sunrise's and sunset's glow, and the hours round noon that are midday.
    constexpr float twilight_deg = -6.0f;
    constexpr float glow_deg = 6.0f;
    constexpr float midday_hours = 1.5f;
    if (sun_elevation_deg < twilight_deg) { return text_hub_part_night; }
    if (sun_elevation_deg < 0.0f) { return before_noon ? text_hub_part_dawn : text_hub_part_dusk; }
    if (sun_elevation_deg < glow_deg) { return before_noon ? text_hub_part_sunrise : text_hub_part_sunset; }
    if (solar_hours_from_noon < midday_hours) { return text_hub_part_midday; }
    return before_noon ? text_hub_part_morning : text_hub_part_afternoon;
}

std::string course_time_text(const course_definition& course, const game_state& game, const text_assets& text,
                             const std::int64_t unix_ms) {
    const sky_timeline& clock = game.tuning.sky_clock;
    const double offset = static_cast<double>(course.sky.utc_offset_hours);
    const double day = game_day_on_date(course_clock_at(clock, course.sky, unix_ms), course_date(clock, course.sky), offset);
    // The sun where the player stands: every course is near enough to it
    // for the part of the day, and its world is not loaded until played.
    const sky_state sky = compute_sky_state(day, static_cast<double>(game.world.latitude_deg),
                                            static_cast<double>(game.world.longitude_deg));
    const float from_noon = 24.0f * (sky.day_fraction - 0.5f);
    const char* part = part_of_day_key(glm::degrees(sky.sun_elevation), from_noon < 0.0f, std::abs(from_noon));
    return format_text(text, text_hub_course_time, {{"time", make_datestamp(text, day, offset).time}, {"part", lookup_text(text, part)}});
}
std::string tee_clock_text(const std::int64_t micros, const game_state& game, const text_assets& text) {
    return make_datestamp(text, game_day_at(game.tuning.sky_clock, micros / 1000), static_cast<double>(game.hub_sky.utc_offset_hours)).time;
}
std::string tee_time_text(const std::int64_t micros, const game_state& game, const text_assets& text) {
    const hub_clock clock = clock_of(game);
    const std::string time = tee_clock_text(micros, game, text);
    return hub_day_at(clock, micros) == hub_day_at(clock, game.online.server_now) + 1 ? format_text(text, text_hub_time_tomorrow, {{"time", time}}) : time;
}
std::string course_figures_text(const startup_course_option& option, const text_assets& text) {
    return format_text(text, text_hub_course_figures, {{"holes", std::to_string(option.course.holes.size())}, {"par", std::to_string(option.total_par)},
        {"length", with_thousands(option.length_meters)}});
}
std::string course_detail_text(const startup_course_option& option, const game_state& game, const text_assets& text, const std::int64_t unix_ms) {
    const course_definition& course = option.course;
    const year_season& season = season_on(game.tuning.trees, day_of_year(course.sky.month, course.sky.day));
    const std::string detail = format_text(text, text_hub_course_detail, {{"there", course_time_text(course, game, text, unix_ms)},
        {"season", lookup_text(text, season_text_key(season.name).c_str())}});
    return course.carts ? detail : format_text(text, text_hub_course_detail_carry_only, {{"detail", detail}, {"carry", lookup_text(text, text_hub_carry_only)}});
}
std::string booking_text(const game_state& game, const startup_catalog& catalog, const text_assets& text) {
    const auto& booking = active_booking(game);
    if (!booking) { return lookup_text(text, text_hub_no_booking); }
    if (booking->scheduled_at == 0) { return course_name(catalog, booking->course_id); }
    const char* key = booking->completed ? text_hub_completed : reservation_expired(game) ? text_hub_expired : booking->started_at != 0 ? text_hub_started : text_hub_booking;
    return format_text(text, key, {{"actual", tee_time_text(booking->started_at, game, text)}, {"course", course_name(catalog, booking->course_id)}, {"time", tee_time_text(booking->scheduled_at, game, text)},
        {"expiry", tee_time_text(booking->scheduled_at + game.tuning.server.tee_expiry_minutes * minute_micros, game, text)}});
}
namespace {
std::string distance_label(const game_state& game, const static_prop& board, const text_assets& text) {
    if (!game.hub) { return {}; }
    for (const auto& target : game.hub->targets) {
        if (horizontal_distance(target.board, board.position) < 1.0f) {
            return format_text(text, text_hub_distance_board, {{"meters", std::to_string(target.distance_meters)}});
        }
    }
    return {};
}
}
std::vector<std::string> prop_board_labels(const game_state& game, const startup_catalog& catalog, const text_assets& text, const std::string& clock) {
    std::vector<std::string> labels;
    int departure_rows = 0;
    for (const auto& prop : game.area.props) {
        const auto& booking = active_booking(game);
        if (prop.model == "tee_book") { labels.push_back(lookup_text(text, text_hub_book_title)); }
        else if (prop.model == "hub_bus") {
            labels.push_back(game.kind != place_kind::hub ? lookup_text(text, text_hub_bus_board_hub)
                : booking ? course_name(catalog, booking->course_id) : lookup_text(text, text_hub_bus_title));
        }
        else if (prop.model == clock_model) { labels.push_back(clock); }
        else if (prop.model == distance_board_model) { labels.push_back(distance_label(game, prop, text)); }
        else if (prop.model == departure_board_model) { labels.push_back(lookup_text(text, text_hub_departures_title)); }
        else if (prop.model == departure_row_model) { labels.push_back(departure_row_label(game, catalog, text, departure_rows++)); }
        else { labels.push_back({}); }
    }
    return labels;
}
std::vector<ui_sound> update_tee_book(tee_book_state& menu, game_state& game, const startup_catalog& catalog,
    const input_state& input, const menu_pointer& pointer, const std::vector<reducer_failure>& refusals) {
    std::vector<ui_sound> sounds;
    for (const auto& failure : refusals) {
        if (failure.reducer == "return_to_hub") {
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
        const bool booked = *menu.pending == net_command_type::book_tee && booking && booking->course_id == menu.pending_course &&
            booking->scheduled_at == menu.scheduled && booking->takes_cart == menu.pending_cart;
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
        send(menu, game, net_command_type::open_tee_book, {menu.course}); return sounds;
    }
    const auto choices = rows(menu, game, catalog);
    std::vector<bool> inert;
    for (const auto& choice : choices) { inert.push_back(choice.kind == row_kind::heading); }
    const auto screen = listing_courses(menu, game) ? startup_menu_screen::course_book : startup_menu_screen::tee_book;
    const int hit = menu.screen == hub_screen::book ? select_course(menu.selection, menu.top, static_cast<int>(choices.size()), input, pointer, sounds, screen, inert) :
        select_with_input(startup_menu_screen::confirm, 1, menu.selection, static_cast<int>(choices.size()), input, pointer, sounds);
    if (!(input.enter.pressed || input.space.pressed || hit >= 0) || choices.empty()) { return sounds; }
    sounds.push_back(ui_sound::select);
    const auto& choice = choices[static_cast<std::size_t>(menu.selection)];
    switch (choice.kind) {
    case row_kind::play_now:
        menu.course = choice.course;
        send(menu, game, net_command_type::book_tee, {choice.course, 0, transport_choice(menu, game, catalog) && has_carts(catalog, choice.course)});
        break;
    case row_kind::course:
        menu.course = choice.course; menu.selection = menu.top = 0;
        send(menu, game, net_command_type::open_tee_book, {menu.course}); break;
    case row_kind::slot:
        if (booking && booking->course_id == choice.course && booking->scheduled_at == choice.time) { break; }
        if (choice.time <= game.online.server_now) { menu.message = text_hub_slot_elapsed; }
        else if (choice.count >= game.tuning.server.group_capacity) { menu.message = text_hub_slot_full; }
        else { send(menu, game, net_command_type::book_tee, {choice.course, choice.time, transport_choice(menu, game, catalog) && has_carts(catalog, choice.course)}); }
        break;
    case row_kind::courses: menu.course.clear(); menu.selection = menu.top = 0; menu.message.clear(); break;
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
    if (!menu.message.empty()) { data.message = lookup_text(text, menu.message.c_str()); }
    if (menu.pending) { data.message = lookup_text(text, text_hub_pending); }
    const auto choices = rows(menu, game, catalog);
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
        case row_kind::slot:
            title = tee_clock_text(row.time, game, text);
            hint = format_text(text, text_hub_count, {{"count", std::to_string(row.count)}, {"capacity", std::to_string(game.tuning.server.group_capacity)}});
            if (const course_definition* course = find_course(catalog, row.course)) {
                hint = format_text(text, text_hub_slot_there, {{"count", hint}, {"there", course_time_text(*course, game, text, row.time / 1000)}});
            }
            if (row.time <= game.online.server_now) { hint = lookup_text(text, text_hub_slot_elapsed); }
            else if (active_booking(game) && active_booking(game)->scheduled_at == row.time && active_booking(game)->course_id == row.course) { hint = format_text(text, text_hub_own_slot, {{"count", std::to_string(row.count)}, {"capacity", std::to_string(game.tuning.server.group_capacity)}}); }
            break;
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
        add_tile(data, title, hint, i == menu.selection);
        data.tiles.back().figures = figures;
        data.tiles.back().preview = preview;
    }
    return data;
}
