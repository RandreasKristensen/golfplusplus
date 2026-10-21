#pragma once

// This frame's sky: the shared clock (shared_unix_ms in game/mode_dispatch.h)
// on the tuning's sky timeline, on the course's date over its world (hub and
// holes alike), lit by the lighting palette. Also the developer's hold on it:
// GOLFPP_GAME_TIME pins the time of day (and the date), and T scrubs the time
// while the Ctrl overlay shows (docs/performance.md). GL-free.

#include "game/game_state.h"
#include "game/lighting_loader.h"
#include "game/weather_lighting.h"
#include "renderer/render_data.h"

#include <cstdint>
#include <optional>

struct sky_controls {
    // The hour (0..24) on the course's clock, so a screenshot shows the same
    // sky every time; none: the clock runs.
    std::optional<float> pinned_hour;
    // Days since 1970-01-01 in place of the course's date.
    std::optional<std::int64_t> pinned_date;
    double scrub_game_days = 0.0;  // added to the clock, wrapping on the date
};

// `held` for `dt` seconds runs the scrub forward.
sky_controls scrub_sky(sky_controls controls, bool held, float dt);

// The game day this frame on a course under `sky`: the clock's time of day,
// on the course's clock and date.
double frame_game_day(const sky_timeline& timeline, const course_sky& sky, const sky_controls& controls,
                      std::int64_t now_unix_ms);

// The game day this frame on the course `game` is on. `local_unix_ms` is this
// machine's UTC clock.
double course_game_day(const game_state& game, const sky_controls& controls, std::int64_t local_unix_ms);

// The hour (0..24) on the course's clock this frame, as course_game_day has it.
float course_hour(const game_state& game, const sky_controls& controls, std::int64_t local_unix_ms);

// The frame's light under the weather as drawn (core/weather_frame.h).
render_lighting make_render_lighting(const game_state& game, const lighting_palette& palette, const sky_controls& controls,
                                     std::int64_t local_unix_ms, const shown_weather& weather = shown_weather{});
