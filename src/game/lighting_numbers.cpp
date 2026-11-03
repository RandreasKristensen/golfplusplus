#include "game/lighting_numbers.h"

#include "game/lighting_fields.h"

#include <cmath>
#include <string>

namespace {
template <typename T, std::size_t count>
std::optional<lighting_number> bounded(const T& object, const bounded_field<T> (&fields)[count], const std::string& key) {
    for (const bounded_field<T>& field : fields) {
        if (key == field.key) {
            return lighting_number{object.*field.member, field.low, field.high, false};
        }
    }
    return std::nullopt;
}

template <typename T, std::size_t count>
std::optional<lighting_number> share(const T& object, const share_field<T> (&fields)[count], const std::string& key) {
    for (const share_field<T>& field : fields) {
        if (key == field.key) {
            return lighting_number{object.*field.member, 0.0f, 1.0f, false};
        }
    }
    return std::nullopt;
}

lighting_number whole(const int value, const int low, const int high) {
    return lighting_number{static_cast<float>(value), static_cast<float>(low), static_cast<float>(high), true};
}

std::optional<lighting_number> key_number(const lighting_palette& palette, const json_path& path) {
    const std::string& index = path[1];
    if (path.size() != 3 || index.empty() || index.size() > 3 || index.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    const std::size_t at = static_cast<std::size_t>(std::stoul(index));
    if (at >= palette.keys.size()) {
        return std::nullopt;
    }
    const lighting_key& key = palette.keys[at];
    const std::string& name = path[2];
    if (name == "exposure") {
        return lighting_number{key.exposure, min_exposure, max_exposure, false};
    }
    if (std::optional<lighting_number> number = bounded(key, key_bounded_fields, name)) {
        return number;
    }
    return share(key, key_share_fields, name);
}

std::optional<lighting_number> shaft_number(const shaft_style& shafts, const std::string& key) {
    if (key == "steps") {
        return whole(shafts.steps, min_shaft_steps, max_shaft_steps);
    }
    if (key == "lamps") {
        return whole(shafts.lamps, 0, max_shaft_lamps);
    }
    return bounded(shafts, shaft_fields, key);
}
}

std::optional<lighting_number> lighting_number_at(const lighting_palette& palette, const json_path& path) {
    if (path.size() >= 2 && path[0] == "keys") {
        return key_number(palette, path);
    }
    if (path.size() != 2) {
        return std::nullopt;
    }
    const std::string& section = path[0];
    const std::string& key = path[1];
    if (section == "sky") {
        return bounded(palette.sky, sky_fields, key);
    }
    if (section == "shadows") {
        return bounded(palette.shadows, shadow_fields, key);
    }
    if (section == "shafts") {
        return shaft_number(palette.shafts, key);
    }
    if (section == "night_lights") {
        return bounded(palette.night_lights, night_light_fields, key);
    }
    if (section == "weather") {
        return bounded(palette.weather, weather_fields, key);
    }
    if (section == "rain") {
        return bounded(palette.rain, rain_fields, key);
    }
    if (section == "wind") {
        return bounded(palette.wind, wind_fields, key);
    }
    if (section == "lightning") {
        return bounded(palette.lightning, lightning_fields, key);
    }
    return std::nullopt;
}

std::size_t nearest_lighting_key(const std::vector<lighting_key>& keys, const float sun_elevation_deg) {
    std::size_t nearest = 0;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (std::abs(keys[i].sun_elevation_deg - sun_elevation_deg) <
            std::abs(keys[nearest].sun_elevation_deg - sun_elevation_deg)) {
            nearest = i;
        }
    }
    return nearest;
}
