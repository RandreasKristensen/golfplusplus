#include "game/weather.h"

#include "game/sky_clock.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
constexpr double hours_per_day = 24.0;
// Each noise its own stream from the course's seed.
constexpr std::uint32_t storm_stream = 0x51u;
constexpr std::uint32_t detail_stream = 0x52u;
constexpr std::uint32_t wind_speed_stream = 0x53u;
constexpr std::uint32_t wind_direction_stream = 0x54u;
constexpr std::uint32_t prevailing_stream = 0x55u;
constexpr std::uint32_t damp_stream = 0x57u;
constexpr std::uint32_t regime_stream = 0x58u;
constexpr std::uint32_t temperature_stream = 0x59u;
// Cloud from this cover on counts as cloudy, thunder from this a thunderstorm.
constexpr float cloudy_cover = 0.5f;
constexpr float thunderstorm_thunder = 0.5f;

// A fixed value in [0, 1) for a lattice point (splitmix64).
float lattice_value(const std::uint32_t seed, const std::uint32_t stream, const std::int64_t index) {
    std::uint64_t x = static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL +
        (static_cast<std::uint64_t>(seed) << 32U) + stream;
    x ^= x >> 30U;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27U;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31U;
    return static_cast<float>(x >> 40U) / 16777216.0f;
}

// Smooth noise in [0, 1) over game hours, its lattice points `period` apart.
// It keeps the two points either side of its last sample, so a caller
// stepping forward hashes each point once.
class noise_track {
public:
    noise_track(const std::uint32_t seed, const std::uint32_t stream, const float period)
        : seed_(seed), stream_(stream), period_(period) {}

    float at(const double hours) {
        if (period_ <= 0.0f) {
            return lattice_value(seed_, stream_, 0);
        }
        const double at = hours / static_cast<double>(period_);
        const double floor = std::floor(at);
        const std::int64_t index = static_cast<std::int64_t>(floor);
        if (!known_ || index != index_) {
            a_ = known_ && index == index_ + 1 ? b_ : lattice_value(seed_, stream_, index);
            b_ = lattice_value(seed_, stream_, index + 1);
            index_ = index;
            known_ = true;
        }
        const float t = static_cast<float>(at - floor);
        const float eased = t * t * (3.0f - 2.0f * t);
        return a_ + (b_ - a_) * eased;
    }

private:
    std::uint32_t seed_;
    std::uint32_t stream_;
    float period_;
    bool known_ = false;
    std::int64_t index_ = 0;
    float a_ = 0.0f;
    float b_ = 0.0f;
};

// The storm: the regime (regime_hours apart) weighing regime_weight, over
// two octaves of passing storms, spell_hours and detail_hours apart.
class storm_track {
public:
    storm_track(const weather_tuning& tuning, const std::uint32_t seed)
        : regime_(seed, regime_stream, tuning.regime_hours),
          broad_(seed, storm_stream, tuning.spell_hours),
          detail_(seed, detail_stream, tuning.detail_hours),
          regime_weight_(clamp01(tuning.regime_weight)),
          detail_weight_(std::max(0.0f, tuning.detail_weight)) {}

    float at(const double hours) {
        const float passing = (broad_.at(hours) + detail_weight_ * detail_.at(hours)) / (1.0f + detail_weight_);
        return regime_weight_ * regime_.at(hours) + (1.0f - regime_weight_) * passing;
    }

private:
    noise_track regime_;
    noise_track broad_;
    noise_track detail_;
    float regime_weight_;
    float detail_weight_;
};

// 0 at `from` or below, 1 at `to` or above, straight between.
float ramp(const float x, const float from, const float to) {
    if (to <= from) {
        return x >= to ? 1.0f : 0.0f;
    }
    return clamp01((x - from) / (to - from));
}

// Clouds follow where the storm is heading: ahead of it by cloud_trend
// times how far it moved since `lagged_storm`, so they gather before the
// rain and break up while it still falls.
float cloud_of(const weather_tuning& tuning, const float storm, const float lagged_storm) {
    return ramp(storm + tuning.cloud_trend * (storm - lagged_storm), tuning.cloud_start, tuning.cloud_full);
}

// The rain from the storm rain_lag_hours ago, thinned to sun_shower_keep of
// it under a clear sky.
float rain_of(const weather_tuning& tuning, const float lagged_storm, const float cloud) {
    const float keep = tuning.sun_shower_keep + (1.0f - tuning.sun_shower_keep) * cloud;
    return ramp(lagged_storm, tuning.rain_start, tuning.rain_full) * keep;
}

// The ground's wetness at `hours`: from dry history_hours before, step by
// step on a grid fixed in game hours, so every caller sums the same steps.
// Rain wets it; the sun dries it as far as the clouds let it through.
float wetness_at(const weather_tuning& tuning, const std::uint32_t seed, const double hours) {
    const double step = static_cast<double>(tuning.step_hours);
    if (step <= 0.0) {
        return 0.0f;
    }
    const std::int64_t last = static_cast<std::int64_t>(std::floor(hours / step));
    const std::int64_t first = last - static_cast<std::int64_t>(std::ceil(static_cast<double>(tuning.history_hours) / step));
    const double lag = static_cast<double>(tuning.rain_lag_hours);
    storm_track now(tuning, seed);
    storm_track behind(tuning, seed);
    float wetness = 0.0f;
    for (std::int64_t i = first; i <= last; ++i) {
        const double start = static_cast<double>(i) * step;
        const double length = std::min(step, hours - start);
        if (length <= 0.0) {
            break;
        }
        const double middle = start + length * 0.5;
        const float lagged = behind.at(middle - lag);
        const float cloud = cloud_of(tuning, now.at(middle), lagged);
        const float rain = rain_of(tuning, lagged, cloud);
        const float change = tuning.wetting_per_hour * rain - tuning.drying_per_hour * (1.0f - cloud);
        wetness = clamp01(wetness + change * static_cast<float>(length));
    }
    return wetness;
}

// Squared, so calm and breezy come more often than strong; rain brings up
// to storm_speed more. The direction is the prevailing one turned by a
// sideways share of it, normalised: no sine or cosine, which may round
// differently in the server's WASM.
glm::vec3 wind_at(const wind_tuning& tuning, const std::uint32_t seed, const double hours, const float rain) {
    const float drift = noise_track(seed, wind_speed_stream, tuning.hours).at(hours);
    const float speed = std::max(tuning.calm_speed,
                                 tuning.calm_speed + (tuning.max_speed - tuning.calm_speed) * drift * drift +
                                     tuning.storm_speed * rain * rain);
    const glm::vec3 prevailing = safe_normalize(glm::vec3(lattice_value(seed, prevailing_stream, 0) * 2.0f - 1.0f, 0.0f,
                                                          lattice_value(seed, prevailing_stream, 1) * 2.0f - 1.0f),
                                                glm::vec3(0.0f, 0.0f, 1.0f));
    const float swing =
        (noise_track(seed, wind_direction_stream, tuning.direction_hours).at(hours) * 2.0f - 1.0f) * tuning.direction_swing;
    return safe_normalize(prevailing + yaw_left(prevailing) * swing, prevailing) * speed;
}
}

std::optional<weather_kind> weather_kind_from_name(const std::string& name) {
    const char* const names[weather_kind_count] = {"clear",      "cloudy", "light_fog",  "fog",         "light_rain",
                                                   "rain",       "heavy_rain", "thunderstorm", "sun_shower"};
    for (std::size_t i = 0; i < weather_kind_count; ++i) {
        if (name == names[i]) {
            return static_cast<weather_kind>(i);
        }
    }
    return std::nullopt;
}

std::uint32_t course_weather_seed(const std::string& course_id) {
    std::uint32_t hash = 2166136261u;  // FNV-1a
    for (const char c : course_id) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

weather_kind rain_kind(const weather_tuning& tuning, const float rain) {
    if (rain < 0.5f * tuning.light_rain) {
        return weather_kind::clear;
    }
    if (rain < 0.5f * (tuning.light_rain + tuning.rain)) {
        return weather_kind::light_rain;
    }
    return rain < 0.5f * (tuning.rain + tuning.heavy_rain) ? weather_kind::rain : weather_kind::heavy_rain;
}

weather_kind fog_kind(const weather_tuning& tuning, const float fog) {
    if (fog < 0.5f * tuning.light_fog) {
        return weather_kind::clear;
    }
    return fog < 0.5f * (tuning.light_fog + tuning.fog) ? weather_kind::light_fog : weather_kind::fog;
}

weather_kind weather_kind_of(const weather_state& weather, const weather_tuning& tuning) {
    const weather_kind rain = rain_kind(tuning, weather.rain);
    if (rain != weather_kind::clear) {
        if (weather.cloud < tuning.sun_shower_cloud) {
            return weather_kind::sun_shower;
        }
        return weather.thunder >= thunderstorm_thunder ? weather_kind::thunderstorm : rain;
    }
    const weather_kind fog = fog_kind(tuning, weather.fog);
    if (fog != weather_kind::clear) {
        return fog;
    }
    return weather.cloud >= cloudy_cover ? weather_kind::cloudy : weather_kind::clear;
}

float rainbow_of(const float rain, const float cloud, const weather_tuning& tuning) {
    if (tuning.sun_shower_cloud <= 0.0f || tuning.light_rain <= 0.0f) {
        return 0.0f;
    }
    return std::min(1.0f, rain / tuning.light_rain) * clamp01(1.0f - cloud / tuning.sun_shower_cloud);
}

weather_state coupled_weather(weather_state weather, const weather_tuning& tuning) {
    weather.cloud = clamp01(weather.cloud);
    weather.rain = std::min(clamp01(weather.rain), tuning.sun_shower_keep + (1.0f - tuning.sun_shower_keep) * weather.cloud);
    const float heavy_share = tuning.heavy_rain > 0.0f ? weather.rain / tuning.heavy_rain : 0.0f;
    weather.thunder = std::min({clamp01(weather.thunder), weather.cloud, heavy_share});
    weather.fog = std::min(clamp01(weather.fog), (1.0f - ramp(weather.rain, 0.0f, tuning.fog_rain_clear)) *
                                                     (1.0f - ramp(wind_speed(weather), tuning.fog_calm_wind, tuning.fog_wind_clear)));
    weather.rainbow = rainbow_of(weather.rain, weather.cloud, tuning);
    weather.kind = weather_kind_of(weather, tuning);
    return weather;
}

weather_state with_weather_kind(weather_state weather, const weather_kind kind, const weather_tuning& tuning) {
    weather.cloud = 0.0f;
    weather.rain = 0.0f;
    weather.fog = 0.0f;
    weather.thunder = 0.0f;
    switch (kind) {
    case weather_kind::clear:
        break;
    case weather_kind::cloudy:
        weather.cloud = 1.0f;
        break;
    case weather_kind::light_fog:
        weather.fog = tuning.light_fog;
        break;
    case weather_kind::fog:
        weather.fog = tuning.fog;
        break;
    case weather_kind::light_rain:
        weather.cloud = 1.0f;
        weather.rain = tuning.light_rain;
        break;
    case weather_kind::rain:
        weather.cloud = 1.0f;
        weather.rain = tuning.rain;
        break;
    case weather_kind::heavy_rain:
        weather.cloud = 1.0f;
        weather.rain = tuning.heavy_rain;
        break;
    case weather_kind::thunderstorm:
        weather.cloud = 1.0f;
        weather.rain = tuning.heavy_rain;
        weather.thunder = 1.0f;
        break;
    case weather_kind::sun_shower:
        weather.rain = tuning.light_rain;
        break;
    }
    weather.rainbow = rainbow_of(weather.rain, weather.cloud, tuning);
    weather.kind = kind;
    return weather;
}

float temperature_at(const weather_tuning& tuning, const course_definition& course, const double hours, const float cloud,
                     const float rain, const float noise) {
    const temperature_tuning& t = tuning.temperature;
    // Local hour of the day, then how far round from the coldest hour to the
    // warmest (rising) or back (falling).
    const double local = hours + static_cast<double>(course.sky.utc_offset_hours);
    const float hour = static_cast<float>(local - hours_per_day * std::floor(local / hours_per_day));
    const float rise_hours = t.warmest_hour - t.coldest_hour;
    const float since_coldest = hour >= t.coldest_hour ? hour - t.coldest_hour : hour + 24.0f - t.coldest_hour;
    const float share = since_coldest <= rise_hours ? since_coldest / rise_hours
                                                    : 1.0f - (since_coldest - rise_hours) / (24.0f - rise_hours);
    const float warmth = share * share * (3.0f - 2.0f * share);  // 0 coldest .. 1 warmest
    const float range = course.air.daily_temperature_range_c * (1.0f - t.cloud_range_damping * clamp01(cloud));
    return course.air.mean_temperature_c + range * (warmth - 0.5f) - t.rain_cooling_c * clamp01(rain) +
        t.noise_c * (2.0f * noise - 1.0f);
}

weather_state weather_at(const weather_tuning& weather,
                         const wind_tuning& wind,
                         const sky_timeline& clock,
                         const course_definition& course,
                         const std::int64_t unix_ms) {
    const std::uint32_t seed = course_weather_seed(course.id);
    const double hours = game_day_at(clock, unix_ms) * hours_per_day;
    weather_state out;
    const float lagged = storm_track(weather, seed).at(hours - static_cast<double>(weather.rain_lag_hours));
    out.cloud = cloud_of(weather, storm_track(weather, seed).at(hours), lagged);
    out.rain = rain_of(weather, lagged, out.cloud);
    out.wind = wind_at(wind, seed, hours, out.rain);
    const float damp = weather.fog_regime_weight * noise_track(seed, regime_stream, weather.regime_hours).at(hours) +
        (1.0f - weather.fog_regime_weight) * noise_track(seed, damp_stream, weather.fog_hours).at(hours);
    out.fog = ramp(damp, weather.fog_start, weather.fog_full);
    out.thunder = ramp(lagged, weather.thunder_start, weather.thunder_full);
    out.wetness = wetness_at(weather, seed, hours);
    out = coupled_weather(out, weather);
    out.temperature_c = temperature_at(weather, course, hours, out.cloud, out.rain,
                                       noise_track(seed, temperature_stream, weather.temperature.noise_hours).at(hours));
    return out;
}

float wind_speed(const weather_state& weather) {
    return glm::length(horizontal(weather.wind));
}

float wind_heading(const weather_state& weather) {
    return yaw_towards(glm::vec3(0.0f), weather.wind);
}
