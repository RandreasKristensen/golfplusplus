#pragma once

// The god menu: a developer's panel over play (F1, with GOLFPP_GOD=1 or
// --god) that holds the time of day, the weather (a kind's preset, then its
// cloud, rain, fog and thunder each on their own), the wind and the ground's
// wetness, to try them. Each row is the live value until it is changed, and
// Backspace lets it run live again. The time of day is the sky's pin
// (core/sky_frame.h), so it is visual only; the rest is the offline weather
// override (game_state::weather_override), which online play ignores: the
// server decides the weather there. GOLFPP_WEATHER starts with the weather
// held. Its last row draws every solid's outline near the camera, for
// checking an import. GL-free.

#include "core/input.h"
#include "core/sky_frame.h"
#include "game/game_state.h"
#include "game/game_tuning.h"
#include "game/text_assets.h"
#include "game/weather.h"
#include "renderer/god_menu_overlay.h"
#include "renderer/solid_outline.h"

#include <cstddef>
#include <cstdint>
#include <optional>

enum class god_menu_row : std::size_t {
    time_of_day,
    weather,
    cloud,
    rain,
    fog,
    thunder,
    wind_speed,
    wind_direction,
    wetness,
    solids
};
inline constexpr std::size_t god_menu_row_count = 10;

struct god_menu_state {
    bool enabled = false;  // GOLFPP_GOD or --god
    bool open = false;
    god_menu_row row = god_menu_row::time_of_day;
    // Held values; none: live.
    std::optional<weather_kind> weather;  // its preset under any amount held below
    std::optional<float> cloud;           // 0..1, as weather_state's
    std::optional<float> rain;
    std::optional<float> fog;
    std::optional<float> thunder;
    std::optional<float> wind_speed;    // m/s at the wind profile's reference height
    std::optional<float> wind_heading;  // radians, the way it blows towards (yaw_direction)
    std::optional<float> wetness;       // 0..1
    bool show_solids = false;  // every solid's outline drawn
};

// What the menu is told each frame, from the keys.
struct god_menu_keys {
    bool toggle = false;  // F1
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool live = false;  // Backspace: the row runs live again
};

struct god_menu_update {
    god_menu_state menu;
    sky_controls sky;
};

// The menu after `keys`. A change to a live row starts from `now` (the
// weather as shown) and `live_hour` (the course's hour now, 0..24). Picking
// a kind lets its cloud, rain, fog and thunder run with it again.
god_menu_update update_god_menu(god_menu_state menu,
                                sky_controls sky,
                                const god_menu_keys& keys,
                                const weather_state& now,
                                float live_hour);

// The weather the menu holds over `live`, or none when every row is live:
// the kind's preset, then each amount held, as the weather allows it
// (coupled_weather: no rain heavier than its cloud lets fall, no thunder
// without cloud and rain, no fog in rain or wind).
std::optional<weather_state> god_weather(const god_menu_state& menu, const weather_state& live, const weather_tuning& tuning);

// The menu as drawn, showing `shown` (this frame's weather) and `hour`
// (0..24 on the course's clock). `online`: the weather rows are the server's.
render_god_menu god_menu_view(const god_menu_state& menu,
                              const sky_controls& sky,
                              const weather_state& shown,
                              float hour,
                              bool online,
                              const text_assets& text);

// F1 and, while the menu is open, the arrows and Backspace.
god_menu_keys god_menu_keys_of(const input_state& input);

// `input` without the keys the open menu takes, so play does not see them.
input_state without_god_menu_keys(input_state input);

// This frame of the menu over `game` (its local_unix_ms set): `keys` change
// it, and the weather it holds is put in game.weather_override.
void run_god_menu(god_menu_state& menu, sky_controls& sky, game_state& game, const god_menu_keys& keys);

// The open menu as drawn over `game`.
// The outlines of every solid within god_solids_reach of `eye` (the
// area's and each green's flagstick), when the menu shows them.
std::vector<world_segment> god_solid_outlines(const god_menu_state& menu, const game_state& game, const glm::vec3& eye);

render_god_menu god_menu_frame_view(const god_menu_state& menu, const sky_controls& sky, const game_state& game,
                                    const text_assets& text);
