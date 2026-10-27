#include "core/god_menu.h"

#include "game/area_solids.h"

#include "game/mode_dispatch.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
// How far one press moves each row.
constexpr float hour_step = 0.5f;
constexpr float wind_speed_step = 0.5f;
constexpr float max_wind_speed = 20.0f;
constexpr float wind_heading_step_degrees = 22.5f;
constexpr float wetness_step = 0.1f;
// Solids are outlined this far round the eye (metres).
constexpr float god_solids_reach = 60.0f;
constexpr float amount_step = 0.1f;
constexpr float hours_per_day = 24.0f;

const char* const weather_names[weather_kind_count] = {
    text_weather_clear,      text_weather_cloudy,       text_weather_light_fog,  text_weather_fog,       text_weather_light_rain,
    text_weather_rain,       text_weather_heavy_rain,   text_weather_thunderstorm, text_weather_sun_shower};
const char* const compass_names[8] = {text_compass_n, text_compass_ne, text_compass_e, text_compass_se,
                                      text_compass_s, text_compass_sw, text_compass_w, text_compass_nw};

god_menu_row step_row(const god_menu_row row, const int by) {
    const int count = static_cast<int>(god_menu_row_count);
    return static_cast<god_menu_row>(((static_cast<int>(row) + by) % count + count) % count);
}

float wrapped_hour(const float hour) {
    return hour - hours_per_day * std::floor(hour / hours_per_day);
}

// The row moved one step `by` (+1 or -1) from `now` when it is live.
void change_row(god_menu_state& menu, sky_controls& sky, const int by, const weather_state& now, const float live_hour) {
    const float step = static_cast<float>(by);
    switch (menu.row) {
    case god_menu_row::time_of_day:
        sky.pinned_hour = wrapped_hour(sky.pinned_hour.value_or(live_hour) + hour_step * step);
        sky.scrub_game_days = 0.0;
        break;
    case god_menu_row::weather: {
        const int count = static_cast<int>(weather_kind_count);
        const int kind = static_cast<int>(menu.weather.value_or(now.kind)) + by;
        menu.weather = static_cast<weather_kind>((kind % count + count) % count);
        menu.cloud.reset();
        menu.rain.reset();
        menu.fog.reset();
        menu.thunder.reset();
        break;
    }
    case god_menu_row::cloud:
        menu.cloud = clamp01(menu.cloud.value_or(now.cloud) + amount_step * step);
        break;
    case god_menu_row::rain:
        menu.rain = clamp01(menu.rain.value_or(now.rain) + amount_step * step);
        break;
    case god_menu_row::fog:
        menu.fog = clamp01(menu.fog.value_or(now.fog) + amount_step * step);
        break;
    case god_menu_row::thunder:
        menu.thunder = clamp01(menu.thunder.value_or(now.thunder) + amount_step * step);
        break;
    case god_menu_row::wind_speed:
        menu.wind_speed = std::clamp(menu.wind_speed.value_or(wind_speed(now)) + wind_speed_step * step, 0.0f, max_wind_speed);
        break;
    case god_menu_row::wind_direction:
        menu.wind_heading = wrap_angle(menu.wind_heading.value_or(wind_heading(now)) +
                                       glm::radians(wind_heading_step_degrees) * step);
        break;
    case god_menu_row::wetness:
        menu.wetness = clamp01(menu.wetness.value_or(now.wetness) + wetness_step * step);
        break;
    case god_menu_row::solids:
        menu.show_solids = !menu.show_solids;
        break;
    }
}

void make_live(god_menu_state& menu, sky_controls& sky) {
    switch (menu.row) {
    case god_menu_row::time_of_day:
        sky.pinned_hour.reset();
        sky.scrub_game_days = 0.0;
        break;
    case god_menu_row::weather:
        menu.weather.reset();
        break;
    case god_menu_row::cloud:
        menu.cloud.reset();
        break;
    case god_menu_row::rain:
        menu.rain.reset();
        break;
    case god_menu_row::fog:
        menu.fog.reset();
        break;
    case god_menu_row::thunder:
        menu.thunder.reset();
        break;
    case god_menu_row::wind_speed:
        menu.wind_speed.reset();
        break;
    case god_menu_row::wind_direction:
        menu.wind_heading.reset();
        break;
    case god_menu_row::wetness:
        menu.wetness.reset();
        break;
    case god_menu_row::solids:
        menu.show_solids = false;
        break;
    }
}

std::string number_text(const char* format, const float value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(value));
    return buffer;
}

std::string hour_text(const text_assets& text, const float hour) {
    const int minutes = static_cast<int>(std::round(wrapped_hour(hour) * 60.0f)) % (24 * 60);
    char hh[4];
    char mm[4];
    std::snprintf(hh, sizeof(hh), "%02d", minutes / 60);
    std::snprintf(mm, sizeof(mm), "%02d", minutes % 60);
    return format_text(text, text_god_hours, {{"hours", hh}, {"minutes", mm}});
}

// The compass point the wind comes from (the way it blows towards, turned
// round). Yaw 0 faces south (+Z) and north is -Z.
std::string compass_text(const text_assets& text, const float heading) {
    const float from_bearing = glm::pi<float>() - heading + glm::pi<float>();
    const float turns = from_bearing / glm::two_pi<float>();
    const int point = static_cast<int>(std::round((turns - std::floor(turns)) * 8.0f)) % 8;
    return lookup_text(text, compass_names[point]);
}

render_god_menu_row row_view(const text_assets& text, const char* label, std::string value, const bool held,
                             const bool selected) {
    render_god_menu_row row;
    row.label = lookup_text(text, label);
    row.value = std::move(value);
    row.held = held;
    row.selected = selected;
    return row;
}
}

god_menu_update update_god_menu(god_menu_state menu,
                                sky_controls sky,
                                const god_menu_keys& keys,
                                const weather_state& now,
                                const float live_hour) {
    if (keys.toggle && menu.enabled) {
        menu.open = !menu.open;
    }
    if (!menu.open) {
        return god_menu_update{menu, sky};
    }
    if (keys.up != keys.down) {
        menu.row = step_row(menu.row, keys.down ? 1 : -1);
    }
    if (keys.left != keys.right) {
        change_row(menu, sky, keys.right ? 1 : -1, now, live_hour);
    }
    if (keys.live) {
        make_live(menu, sky);
    }
    return god_menu_update{menu, sky};
}

std::optional<weather_state> god_weather(const god_menu_state& menu, const weather_state& live, const weather_tuning& tuning) {
    if (!menu.weather && !menu.cloud && !menu.rain && !menu.fog && !menu.thunder && !menu.wind_speed && !menu.wind_heading &&
        !menu.wetness) {
        return std::nullopt;
    }
    weather_state held = menu.weather ? with_weather_kind(live, *menu.weather, tuning) : live;
    held.cloud = menu.cloud.value_or(held.cloud);
    held.rain = menu.rain.value_or(held.rain);
    held.fog = menu.fog.value_or(held.fog);
    held.thunder = menu.thunder.value_or(held.thunder);
    if (menu.wind_speed || menu.wind_heading) {
        const float speed = menu.wind_speed.value_or(wind_speed(live));
        held.wind = yaw_direction(menu.wind_heading.value_or(wind_heading(live))) * speed;
    }
    if (menu.wetness) {
        held.wetness = *menu.wetness;
    }
    return coupled_weather(held, tuning);
}

render_god_menu god_menu_view(const god_menu_state& menu,
                              const sky_controls& sky,
                              const weather_state& shown,
                              const float hour,
                              const bool online,
                              const text_assets& text) {
    const auto selected = [&menu](const god_menu_row row) { return menu.open && menu.row == row; };
    const bool offline = !online;
    render_god_menu view;
    view.title = lookup_text(text, text_god_title);
    view.rows.push_back(row_view(text, text_god_time, hour_text(text, hour), sky.pinned_hour.has_value(),
                                 selected(god_menu_row::time_of_day)));
    view.rows.push_back(row_view(text, text_god_weather, lookup_text(text, weather_names[static_cast<std::size_t>(shown.kind)]),
                                 offline && menu.weather.has_value(), selected(god_menu_row::weather)));
    const auto amount_row = [&](const char* label, const float amount, const std::optional<float>& held, const god_menu_row row) {
        view.rows.push_back(row_view(text, label, format_text(text, text_god_percent, {{"percent", number_text("%.0f", amount * 100.0f)}}),
                                     offline && (held.has_value() || menu.weather.has_value()), selected(row)));
    };
    amount_row(text_god_cloud, shown.cloud, menu.cloud, god_menu_row::cloud);
    amount_row(text_god_rain, shown.rain, menu.rain, god_menu_row::rain);
    amount_row(text_god_fog, shown.fog, menu.fog, god_menu_row::fog);
    amount_row(text_god_thunder, shown.thunder, menu.thunder, god_menu_row::thunder);
    view.rows.push_back(row_view(text, text_god_wind, format_text(text, text_god_speed, {{"speed", number_text("%.1f", wind_speed(shown))}}),
                                 offline && menu.wind_speed.has_value(), selected(god_menu_row::wind_speed)));
    view.rows.push_back(row_view(text, text_god_wind_direction, compass_text(text, wind_heading(shown)),
                                 offline && menu.wind_heading.has_value(), selected(god_menu_row::wind_direction)));
    view.rows.push_back(row_view(text, text_god_wetness,
                                 format_text(text, text_god_percent, {{"percent", number_text("%.0f", shown.wetness * 100.0f)}}),
                                 offline && menu.wetness.has_value(), selected(god_menu_row::wetness)));
    view.rows.push_back(row_view(text, text_god_solids, lookup_text(text, menu.show_solids ? text_god_on : text_god_off),
                                 menu.show_solids, selected(god_menu_row::solids)));
    view.note = lookup_text(text, online ? text_god_online : text_god_keys);
    return view;
}

god_menu_keys god_menu_keys_of(const input_state& input) {
    return god_menu_keys{input.key_f1.pressed, input.up.pressed, input.down.pressed, input.left.pressed,
                         input.right.pressed, input.backspace.pressed};
}

input_state without_god_menu_keys(input_state input) {
    input.up = button_state{};
    input.down = button_state{};
    input.left = button_state{};
    input.right = button_state{};
    input.backspace = button_state{};
    return input;
}

void run_god_menu(god_menu_state& menu, sky_controls& sky, game_state& game, const god_menu_keys& keys) {
    game.weather_override.reset();
    const weather_state live = course_weather_at(game, shot_unix_ms(game));
    const weather_state shown = god_weather(menu, live, game.tuning.weather).value_or(live);
    const god_menu_update next = update_god_menu(menu, sky, keys, shown, course_hour(game, sky, game.local_unix_ms));
    menu = next.menu;
    sky = next.sky;
    game.weather_override = god_weather(menu, live, game.tuning.weather);
}

std::vector<world_segment> god_solid_outlines(const god_menu_state& menu, const game_state& game, const glm::vec3& eye) {
    std::vector<world_segment> outlines;
    if (!menu.show_solids) {
        return outlines;
    }
    const play_area& area = game.area;
    const glm::vec3 reach(god_solids_reach, 0.0f, god_solids_reach);
    const auto add = [&](const solid& thing) {
        const std::vector<world_segment> edges = solid_outline(thing);
        outlines.insert(outlines.end(), edges.begin(), edges.end());
    };
    for (const std::uint32_t index : solids_near(area.solids_grid, eye - reach, eye + reach, 0.0f)) {
        add(area.solids[index]);
    }
    for (const solid& stick : flagstick_solids(area.cups, game.flags_in, game.tuning)) {
        if (horizontal_distance(stick.base, eye) <= god_solids_reach) {
            add(stick);
        }
    }
    return outlines;
}

render_god_menu god_menu_frame_view(const god_menu_state& menu, const sky_controls& sky, const game_state& game,
                                    const text_assets& text) {
    return god_menu_view(menu, sky, course_weather_at(game, shot_unix_ms(game)), course_hour(game, sky, game.local_unix_ms),
                         is_online(game), text);
}
