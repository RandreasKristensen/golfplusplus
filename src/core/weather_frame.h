#pragma once

// The weather as drawn: game_state::weather faded in and out over the
// palette's fade_seconds so a change of sky never snaps, the rain and the
// wind's specks moving through their volumes round the camera, every
// flag's flap and the lightning (core/lightning_frame.h). Presentation only, kept by app and run on frame dt; the game
// never reads it. GL-free.

#include "core/lightning_frame.h"
#include "game/game_state.h"
#include "game/lighting_loader.h"
#include "game/weather.h"
#include "game/weather_lighting.h"
#include "physics/physics_tuning.h"
#include "renderer/render_data.h"

#include <glm/vec3.hpp>

struct weather_presentation {
    bool started = false;  // false: the next frame starts at the weather as it is
    shown_weather shown;
    glm::vec3 rain_offset{0.0f};   // within the rain's volume
    glm::vec3 speck_offset{0.0f};  // within the specks'
    float speck_tumble_phase = 0.0f;
    float flag_phase = 0.0f;
    lightning_presentation lightning;
};

// What `weather` shows once faded in.
shown_weather shown_target(const weather_state& weather);

// `shown` eased towards `target` over `dt`, most of the way in `seconds`.
float eased_toward(float shown, float target, float dt, float seconds);

// `offset` moved back into [0, size) on every axis.
glm::vec3 wrapped_in_volume(const glm::vec3& offset, const glm::vec3& size);

// The rain's box, round the camera, and the specks', from the ground under it
// up to twice their height.
glm::vec3 rain_volume(const rain_style& style);
glm::vec3 speck_volume(const wind_style& style);

// Rain at amount `rain` (0..1) falling through the wind at the rain's height.
glm::vec3 rain_velocity(const weather_state& weather, float rain, const rain_style& style, const wind_profile& profile);

// The wind's specks, carried at the wind of their height.
glm::vec3 speck_velocity(const weather_state& weather, const wind_style& style, const wind_profile& profile);

// The level wind at a flag `flag_height` metres up.
glm::vec3 flag_wind(const weather_state& weather, float flag_height, const wind_profile& profile);

// How far a flag in `wind` streams out, 0 hanging .. 1 straight out.
float flag_lift(const glm::vec3& wind, const wind_style& style);

// One frame of `dt` seconds towards `weather`; an unstarted one starts at
// it. Lightning strikes only while `strikes_allowed`.
weather_presentation update_weather_presentation(weather_presentation state, const weather_state& weather,
                                                 const lighting_palette& palette, const wind_profile& profile,
                                                 float flag_height, float dt, bool strikes_allowed);

// What the shown weather does to the sky, haze and ground.
render_sky_weather make_render_sky_weather(const shown_weather& shown, const weather_style& style);

// `camera` is where the scene is seen from, `ground_height` the ground's
// height under it.
render_weather make_render_weather(const weather_presentation& state, const weather_state& weather,
                                   const lighting_palette& palette, const wind_profile& profile, float flag_height,
                                   const glm::vec3& camera, float ground_height);

// The frame's: `state` run on towards game.weather for `dt` while `playing`
// (menus show the weather as it is), lightning striking only while I walk,
// and what it draws, by `data`'s camera, flags and player.
weather_presentation update_frame_weather(const weather_presentation& state, const game_state& game,
                                          const lighting_palette& palette, const render_data& data, bool playing, float dt);
render_weather make_frame_weather(const weather_presentation& state, const game_state& game, const lighting_palette& palette,
                                  const render_data& data);
