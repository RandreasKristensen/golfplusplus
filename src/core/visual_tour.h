#pragma once

// The visual tour (VISUAL TOUR on the main menu, GOLFPP_TOUR): no play, only
// the picture. A camera flies round the course along every hole's line of
// play, tee to green and on to the next tee, over the ground and the trees,
// while the tour steps through its stops (game/visual_tour_loader.h): each
// pins the sky at its hour and holds its weather, with its title burned in
// like a camcorder's. Enter goes on to the next stop, Escape leaves. Offline
// only, and visual only: nothing is played, scored or saved. GL-free.

#include "core/sky_frame.h"
#include "game/game_state.h"
#include "game/text_assets.h"
#include "game/visual_tour_loader.h"
#include "game/weather.h"
#include "renderer/camera_transition.h"

#include <cstddef>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// The flight round the course, one point every tour_path_spacing metres,
// closing back to its start.
struct tour_path {
    std::vector<glm::vec3> eye;     // where the camera flies
    std::vector<glm::vec3> ground;  // the ground under each
    float length = 0.0f;            // horizontal, round the loop
};

inline constexpr float tour_path_spacing = 4.0f;

struct tour_state {
    tour_path path;
    std::size_t stop = 0;
    float stop_seconds = 0.0f;  // into the stop
    float from_hour = 0.0f;     // the hour shown as the stop began
    float travelled = 0.0f;     // along the path, wrapping
};

// The path round `game`'s course: every hole's line of play in order, from
// the last green back to the first tee.
tour_path make_tour_path(const game_state& game, const tour_flight& flight);

// The tour on `game`'s course from stop `first` (wrapped to the stops there are).
tour_state start_tour(const game_state& game, const visual_tour& tour, std::size_t first);

// `dt` seconds on; `next` moves on to the next stop at once.
tour_state advance_tour(tour_state state, const visual_tour& tour, float dt, bool next);

// Where the camera is and what it looks at; its field of view is the player's.
camera_view tour_camera(const tour_state& state, const tour_flight& flight);

// The sky the tour pins on its date: the stop's hour, reached from the
// hour shown as it began over the tour's transition_seconds, the clock
// running forwards.
sky_controls tour_sky(const tour_state& state, const visual_tour& tour);

// The weather the stop holds over `live`: its kind's preset, its wetness.
weather_state tour_weather(const tour_state& state, const visual_tour& tour, const weather_state& live,
                           const weather_tuning& tuning);

// The stop's title while it shows; empty after title_seconds.
std::string tour_title(const tour_state& state, const visual_tour& tour, const text_assets& text);
