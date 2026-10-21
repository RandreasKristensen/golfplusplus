#include "game/lighting_loader.h"

#include "game/content_files.h"
#include "game/json_util.h"
#include "game/lighting_fields.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>

#include <glm/vector_relational.hpp>

namespace {
bool is_color(const glm::vec3& color) {
    return !glm::any(glm::lessThan(color, glm::vec3(0.0f))) && !glm::any(glm::greaterThan(color, glm::vec3(1.0f)));
}

// Three numbers in 0..1.
std::optional<glm::vec3> read_color(const json& object, const char* key) {
    const std::optional<glm::vec3> color = json_vec3(object, key);
    return color && is_color(*color) ? color : std::nullopt;
}

template <typename T, std::size_t count>
bool read_colors(const json& root, const char* section, const color_field<T> (&fields)[count], T& out) {
    const json* object = json_object(root, section);
    if (object == nullptr) {
        return false;
    }
    for (const color_field<T>& field : fields) {
        const std::optional<glm::vec3> color = read_color(*object, field.key);
        if (!color) {
            return false;
        }
        out.*field.member = *color;
    }
    return true;
}

// A non-empty array of colours.
std::optional<std::vector<glm::vec3>> read_color_list(const json& object, const char* key) {
    const json* array = json_array(object, key);
    std::optional<std::vector<glm::vec3>> colors = array != nullptr ? json_vec3_array(*array) : std::nullopt;
    if (!colors || colors->empty() || !std::all_of(colors->begin(), colors->end(), is_color)) {
        return std::nullopt;
    }
    return colors;
}

// A number in 0..1.
std::optional<float> read_share(const json& object, const char* key) {
    const std::optional<float> value = json_float(object, key);
    return value && *value >= 0.0f && *value <= 1.0f ? value : std::nullopt;
}

template <typename T, std::size_t count>
bool read_bounded(const json& object, const bounded_field<T> (&fields)[count], T& out) {
    for (const bounded_field<T>& field : fields) {
        const std::optional<float> value = json_float(object, field.key);
        if (!value || *value < field.low || *value > field.high) {
            return false;
        }
        out.*field.member = *value;
    }
    return true;
}

std::optional<std::vector<lighting_key>> parse_keys(const json& root) {
    const json* array = json_array(root, "keys");
    if (array == nullptr || array->empty()) {
        return std::nullopt;
    }
    std::vector<lighting_key> keys;
    for (const json& value : *array) {
        lighting_key key;
        const std::optional<float> elevation = json_float(value, "sun_elevation_deg");
        const std::optional<float> exposure = json_float(value, "exposure");
        if (!value.is_object() || !elevation || *elevation < -90.0f || *elevation > 90.0f ||
            (!keys.empty() && !(*elevation > keys.back().sun_elevation_deg)) || !exposure || *exposure < min_exposure ||
            *exposure > max_exposure) {
            return std::nullopt;
        }
        key.sun_elevation_deg = *elevation;
        key.exposure = *exposure;
        for (const color_field<lighting_key>& field : key_fields) {
            const std::optional<glm::vec3> color = read_color(value, field.key);
            if (!color) {
                return std::nullopt;
            }
            key.*field.member = *color;
        }
        for (const share_field<lighting_key>& field : key_share_fields) {
            const std::optional<float> share = read_share(value, field.key);
            if (!share) {
                return std::nullopt;
            }
            key.*field.member = *share;
        }
        keys.push_back(key);
    }
    return keys;
}

std::optional<glow_floors> parse_glow(const json& root) {
    const json* object = json_object(root, "glow");
    if (object == nullptr) {
        return std::nullopt;
    }
    glow_floors glow;
    for (const share_field<glow_floors>& field : glow_fields) {
        const std::optional<float> share = read_share(*object, field.key);
        if (!share) {
            return std::nullopt;
        }
        glow.*field.member = *share;
    }
    return glow;
}

std::optional<sky_style> parse_sky(const json& root) {
    const json* object = json_object(root, "sky");
    if (object == nullptr) {
        return std::nullopt;
    }
    sky_style sky;
    if (!read_bounded(*object, sky_fields, sky)) {
        return std::nullopt;
    }
    const std::optional<glm::vec3> moon_color = read_color(*object, "moon_color");
    const std::optional<glm::vec3> cloud_shade = read_color(*object, "cloud_shade");
    if (!moon_color || !cloud_shade || !(sky.stars_full_deg < sky.stars_appear_deg) ||
        !(sky.rainbow_radius_deg < sky.rainbow_secondary_radius_deg)) {
        return std::nullopt;
    }
    sky.moon_color = *moon_color;
    sky.cloud_shade = *cloud_shade;
    return sky;
}

std::optional<shadow_style> parse_shadows(const json& root) {
    const json* object = json_object(root, "shadows");
    shadow_style shadows;
    if (object == nullptr || !read_bounded(*object, shadow_fields, shadows) ||
        !(shadows.sun_fade_start_deg < shadows.sun_fade_full_deg)) {
        return std::nullopt;
    }
    const std::optional<int> resolution = json_int(*object, "map_resolution");
    if (!resolution || *resolution < min_shadow_map_resolution || *resolution > max_shadow_map_resolution ||
        (*resolution & (*resolution - 1)) != 0) {
        return std::nullopt;
    }
    shadows.map_resolution = *resolution;
    return shadows;
}

std::optional<night_light> parse_night_light(const json& object, const char* key) {
    const json* light_object = json_object(object, key);
    night_light light;
    if (light_object == nullptr || !read_bounded(*light_object, light_fields, light)) {
        return std::nullopt;
    }
    const std::optional<glm::vec3> value = read_color(*light_object, "color");
    if (!value) {
        return std::nullopt;
    }
    light.color = *value;
    return light;
}

std::optional<night_light_style> parse_night_lights(const json& root) {
    const json* object = json_object(root, "night_lights");
    night_light_style lights;
    if (object == nullptr || !read_bounded(*object, night_light_fields, lights) || !(lights.full_deg < lights.on_deg) ||
        !(lights.headlight_soft_deg < lights.headlight_cone_deg) ||
        !(lights.floodlight_soft_deg < lights.floodlight_cone_deg)) {
        return std::nullopt;
    }
    const std::optional<night_light> ball = parse_night_light(*object, "ball");
    const std::optional<night_light> headlight = parse_night_light(*object, "headlight");
    const std::optional<night_light> floodlight = parse_night_light(*object, "floodlight");
    if (!ball || !headlight || !floodlight) {
        return std::nullopt;
    }
    lights.ball = *ball;
    lights.headlight = *headlight;
    lights.floodlight = *floodlight;
    return lights;
}

std::optional<weather_style> parse_weather(const json& root) {
    const json* object = json_object(root, "weather");
    weather_style weather;
    if (object == nullptr || !read_bounded(*object, weather_fields, weather)) {
        return std::nullopt;
    }
    const std::optional<glm::vec3> sky = read_color(*object, "overcast_sky");
    if (!sky) {
        return std::nullopt;
    }
    weather.overcast_sky = *sky;
    return weather;
}

// A count of streaks within 1..max_weather_streaks.
std::optional<int> read_streak_count(const json& object, const char* key) {
    const std::optional<int> count = json_int(object, key);
    return count && *count >= 1 && *count <= max_weather_streaks ? count : std::nullopt;
}

std::optional<rain_style> parse_rain(const json& root) {
    const json* object = json_object(root, "rain");
    rain_style rain;
    if (object == nullptr || !read_bounded(*object, rain_fields, rain)) {
        return std::nullopt;
    }
    const std::optional<int> drops = read_streak_count(*object, "max_drops");
    const std::optional<glm::vec3> color = read_color(*object, "color");
    if (!drops || !color) {
        return std::nullopt;
    }
    rain.max_drops = *drops;
    rain.color = *color;
    return rain;
}

std::optional<lightning_style> parse_lightning(const json& root) {
    const json* object = json_object(root, "lightning");
    lightning_style lightning;
    if (object == nullptr || !read_bounded(*object, lightning_fields, lightning) ||
        lightning.top_min_deg > lightning.top_max_deg || lightning.near_meters > lightning.far_meters) {
        return std::nullopt;
    }
    const std::optional<glm::vec3> color = read_color(*object, "color");
    if (!color) {
        return std::nullopt;
    }
    lightning.color = *color;
    return lightning;
}

std::optional<wind_style> parse_wind(const json& root) {
    const json* object = json_object(root, "wind");
    wind_style wind;
    if (object == nullptr || !read_bounded(*object, wind_fields, wind) || !(wind.flag_calm_speed < wind.flag_full_speed)) {
        return std::nullopt;
    }
    const std::optional<int> specks = read_streak_count(*object, "max_specks");
    const std::optional<glm::vec3> color = read_color(*object, "speck_color");
    if (!specks || !color) {
        return std::nullopt;
    }
    wind.max_specks = *specks;
    wind.speck_color = *color;
    return wind;
}

std::optional<figure_colors> parse_figures(const json& root) {
    figure_colors figures;
    const json* object = json_object(root, "figures");
    if (object == nullptr || !read_colors(root, "figures", figure_fields, figures)) {
        return std::nullopt;
    }
    std::optional<std::vector<glm::vec3>> shirts = read_color_list(*object, "shirts");
    std::optional<std::vector<glm::vec3>> group_caps = read_color_list(*object, "group_caps");
    const std::optional<float> share = json_float(*object, "ball_shirt_share");
    if (!shirts || !group_caps || !share || *share < 0.0f || *share > 1.0f) {
        return std::nullopt;
    }
    figures.shirts = std::move(*shirts);
    figures.group_caps = std::move(*group_caps);
    figures.ball_shirt_share = *share;
    return figures;
}
}

std::optional<lighting_palette> parse_lighting_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }
    std::optional<std::vector<lighting_key>> keys = parse_keys(*root);
    std::optional<figure_colors> figures = parse_figures(*root);
    const std::optional<glow_floors> glow = parse_glow(*root);
    const std::optional<sky_style> sky = parse_sky(*root);
    const std::optional<shadow_style> shadows = parse_shadows(*root);
    const std::optional<night_light_style> night_lights = parse_night_lights(*root);
    const std::optional<weather_style> weather = parse_weather(*root);
    const std::optional<rain_style> rain = parse_rain(*root);
    const std::optional<wind_style> wind = parse_wind(*root);
    const std::optional<lightning_style> lightning = parse_lightning(*root);
    lighting_palette palette;
    scene_colors& colors = palette.colors;
    if (!keys || !figures || !glow || !sky || !shadows || !night_lights || !weather || !rain || !wind || !lightning ||
        !read_colors(*root, "ground", ground_fields, colors.ground) ||
        !read_colors(*root, "markers", marker_fields, colors.markers)) {
        return std::nullopt;
    }
    palette.keys = std::move(*keys);
    palette.sky = *sky;
    palette.shadows = *shadows;
    palette.night_lights = *night_lights;
    palette.weather = *weather;
    palette.rain = *rain;
    palette.wind = *wind;
    palette.lightning = *lightning;
    colors.figures = std::move(*figures);
    colors.glow = *glow;
    return palette;
}

std::optional<lighting_palette> load_lighting(const std::string& asset_root) {
    const std::optional<std::string> text = read_text_file(std::filesystem::path(asset_root) / lighting_path);
    return text ? parse_lighting_from_text(*text) : std::nullopt;
}
