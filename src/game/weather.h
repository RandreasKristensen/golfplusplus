#pragma once

// The weather over a course: wind, cloud, rain, fog and how wet the ground
// is, each a number that moves with the others. A pure function of the
// course and UTC time on the sky's clock (game hours, game_day_at), like the
// time of day: the same for everyone on a course at the same moment, with no
// row or save field, so the server replays a shot in the weather it was hit
// in (shot_input::shot_at). Unlike the time of day it changes play: wind
// and rain in the air, wet ground on landing (fog only hides things).
//
// Noises drive it. The slowest is the regime, days long: settled spells of
// sun, or unsettled ones in which the storm noise, hours long, keeps
// bringing rain and cloud by turns for days. Clouds run ahead of the storm;
// rain follows it a few hours behind and only falls in full under cloud, so
// a storm builds before it rains and a clearing sky lets the rain thin out
// in sunshine (a sun shower, its rainbow). The biggest storms thunder. Fog
// lies where the air is damp, damper in unsettled spells, through drizzle
// but cleared by rain and wind. The ground wets in rain and dries in the
// sun, summed over the last hours. Plain arithmetic only (no sine, cosine or
// powers), so the server's WASM gives the very same weather; it costs a few
// hundred multiplications a call, each noise point hashed once.

#include "game/game_tuning.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <glm/vec3.hpp>

// What the weather looks like at a glance, read off its numbers
// (weather_kind_of): the god menu's presets and the rangefinder's limits.
enum class weather_kind : std::uint8_t {
    clear,
    cloudy,
    light_fog,
    fog,
    light_rain,
    rain,
    heavy_rain,
    // Heavy rain with lightning and thunder.
    thunderstorm,
    // Rain falling through a clearing sky: a rainbow opposite the sun.
    sun_shower
};
inline constexpr std::size_t weather_kind_count = 9;

struct weather_state {
    weather_kind kind = weather_kind::clear;
    float cloud = 0.0f;    // the sky's cover, 0 clear .. 1 overcast
    float rain = 0.0f;     // 0 dry .. 1 heaviest
    float fog = 0.0f;      // 0 clear .. 1 thickest
    float rainbow = 0.0f;  // a sun shower's rainbow, 0 none .. 1 brightest
    float thunder = 0.0f;  // how thundery, 0 none .. 1 lightning at its most often
    glm::vec3 wind{0.0f};  // m/s at the wind profile's reference height, level
    float wetness = 0.0f;  // the ground, 0 dry .. 1 soaked
};

// "clear", "cloudy", "light_fog", "fog", "light_rain", "rain", "heavy_rain",
// "thunderstorm" or "sun_shower".
std::optional<weather_kind> weather_kind_from_name(const std::string& name);

// A course's own weather: a hash of its id.
std::uint32_t course_weather_seed(const std::string& course_id);

// The rain's kind on its own: none (clear), light_rain, rain or heavy_rain,
// whichever preset amount (weather.light_rain, rain, heavy_rain) `rain` is
// nearest. The fog's likewise: clear, light_fog or fog.
weather_kind rain_kind(const weather_tuning& tuning, float rain);
weather_kind fog_kind(const weather_tuning& tuning, float fog);

// The kind `weather`'s numbers look like: a sun shower when it rains under
// less cloud than weather.sun_shower_cloud, a thunderstorm from half
// thunder, else the rain's kind; dry, the fog's; else cloudy from half cover.
weather_kind weather_kind_of(const weather_state& weather, const weather_tuning& tuning);

// A sun shower's rainbow for `rain` under `cloud`: brighter the clearer the
// sky and the nearer the rain is to light rain's amount.
float rainbow_of(float rain, float cloud, const weather_tuning& tuning);

// `weather` as the weather allows it, whatever its numbers were set to:
// rain no heavier than its cloud lets fall (sun_shower_keep of it under a
// clear sky), thunder no more than its cloud or its share of heavy rain,
// no thicker fog than rain (none by fog_rain_clear) and wind (none from
// fog_wind_clear, all of it to fog_calm_wind) leave, its rainbow and kind
// read off the rest; coupling it twice changes nothing. weather_at's weather is already so; the god menu's is made so.
weather_state coupled_weather(weather_state weather, const weather_tuning& tuning);

// `weather` set to `kind`'s preset cloud, rain, fog and thunder, the rest kept.
weather_state with_weather_kind(weather_state weather, weather_kind kind, const weather_tuning& tuning);

// The weather on the course seeded `seed` at `unix_ms` (real UTC milliseconds).
weather_state weather_at(const weather_tuning& weather,
                         const wind_tuning& wind,
                         const sky_timeline& clock,
                         std::uint32_t seed,
                         std::int64_t unix_ms);

// The wind's speed (m/s) and the direction it blows towards (radians, as
// yaw_direction in physics/vector_math.h), for showing it.
float wind_speed(const weather_state& weather);
float wind_heading(const weather_state& weather);
