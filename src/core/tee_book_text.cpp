// What the tee book and the hub's boards say: tee times on the hub's in-game
// clock, a course's time and season there, and the booking line.
#include "core/tee_book.h"
#include "core/departure_board.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/model_kinds.h"
#include "physics/vector_math.h"
#include "core/vcr_frame.h"
#include "game/sky_clock.h"
#include "game/tee_times.h"
#include "game/tree_season.h"
#include "game/text_ids.h"
#include <cmath>

#include <glm/trigonometric.hpp>

hub_clock tee_book_clock(const game_state& game) {
    return hub_clock{game.tuning.sky_clock, static_cast<double>(game.hub_sky.utc_offset_hours)};
}
const course_definition* catalog_course(const startup_catalog& catalog, const std::string& id) {
    for (const auto& c : catalog.courses) { if (c.course.id == id) { return &c.course; } }
    return nullptr;
}

std::string with_thousands(const int value) {
    std::string digits = std::to_string(value);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) { digits.insert(static_cast<std::size_t>(at), ","); }
    return digits;
}

std::string course_name(const startup_catalog& catalog, const std::string& id) {
    const course_definition* course = catalog_course(catalog, id);
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
    const hub_clock clock = tee_book_clock(game);
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
