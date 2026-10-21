#include "game/rangefinder.h"

#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/progress_rules.h"
#include "game/remote_players.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Its sight of a flag is to the cloth this far up the stick, and counts as
// clear when nothing stands nearer than this share short of it.
constexpr float flag_cloth_share = 0.8f;
constexpr float flag_sight_slack = 0.03f;

// A fixed value in [0, 1) for reading `ping`'s roll `stream` (splitmix64).
float ping_roll(const std::uint32_t ping, const std::uint32_t stream) {
    std::uint64_t x = static_cast<std::uint64_t>(ping) * 0x9E3779B97F4A7C15ULL + stream;
    x ^= x >> 30U;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27U;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31U;
    return static_cast<float>(x >> 40U) / 16777216.0f;
}

// The point of the flagstick from `foot` to `height` above it nearest the
// line from `eye` along `look`.
glm::vec3 nearest_on_flagstick(const glm::vec3& eye, const glm::vec3& look, const glm::vec3& foot, const float height) {
    const glm::vec3 from_foot = eye - foot;
    const float b = glm::dot(look, world_up);
    const float denom = 1.0f - b * b;
    const float along = denom > 0.00001f ? (glm::dot(world_up, from_foot) - b * glm::dot(look, from_foot)) / denom : 0.0f;
    return foot + world_up * std::clamp(along, 0.0f, height);
}
}

glm::vec3 first_person_eye(const game_state& state) {
    return state.player.position + world_up * state.tuning.camera.walking_eye_height;
}

glm::vec3 first_person_look(const game_state& state) {
    const float pitch = state.look.pitch;
    return yaw_direction(view_yaw(state)) * std::cos(pitch) + world_up * std::sin(pitch);
}

std::optional<glm::vec3> ground_hit(const play_area& area,
                                    const glm::vec3& eye,
                                    const glm::vec3& direction,
                                    const rangefinder_tuning& tuning,
                                    const world_scale_tuning& scale) {
    const float units = 1.0f / std::max(0.0001f, scale.meters_per_world_unit);
    const float range = tuning.max_range_meters * units;
    const float step = std::max(0.01f, tuning.march_step_meters * units);
    const auto below = [&](const float t) {
        const glm::vec3 point = eye + direction * t;
        return point.y <= terrain_height(area, point);
    };
    float above = 0.0f;
    while (above < range) {
        float beneath = std::min(above + step, range);
        if (below(beneath)) {
            for (int i = 0; i < tuning.refine_steps; ++i) {
                const float middle = 0.5f * (above + beneath);
                (below(middle) ? beneath : above) = middle;
            }
            return eye + direction * beneath;
        }
        above = beneath;
    }
    return std::nullopt;
}

rangefinder_view rangefinder_view_of(const game_state& state) {
    const rangefinder_tuning& tuning = state.tuning.rangefinder;
    const cart_tuning& cart = state.tuning.cart;
    rangefinder_view view;
    view.scene.area = &state.area;
    view.scene.trees = &state.static_anchors.trees;
    for (const cart_in_world& shown : carts_in_world(state)) {
        const glm::vec3 forward = yaw_direction(shown.pose.yaw);
        const glm::vec3 middle = shown.pose.position + forward * (0.5f * (cart.body_ahead - cart.body_behind)) +
            world_up * (0.5f * tuning.cart_height);
        view.scene.boxes.push_back(sight_box{
            middle, forward,
            glm::vec3(0.5f * (cart.body_ahead + cart.body_behind) + cart.body_radius, 0.5f * tuning.cart_height, cart.body_radius)});
    }
    for (const auto& [account, avatar] : state.remote_avatars) {
        view.scene.posts.push_back(sight_post{avatar.position, state.tuning.player.body_radius, tuning.person_height});
    }
    view.flags = state.static_anchors.hub_pin_markers;
    if (state.hole) {
        const std::size_t index = state.round.current_hole_index;
        if (index < view.flags.size()) {
            view.flags[index] = pin_anchor_position(state);
        } else {
            view.flags.push_back(pin_anchor_position(state));
        }
    }
    const world_scale_tuning& scale = state.tuning.scale;
    view.flag_height = scale.pin_visual_height_meters / std::max(0.0001f, scale.meters_per_world_unit);
    return view;
}

rangefinder_reading read_rangefinder(const play_area& area,
                                     const rangefinder_view& view,
                                     const glm::vec3& player,
                                     const glm::vec3& eye,
                                     const glm::vec3& look,
                                     const rangefinder_tuning& tuning,
                                     const world_scale_tuning& scale) {
    const float range = tuning.max_range_meters / std::max(0.0001f, scale.meters_per_world_unit);
    // How far the first thing along `direction` is, within `limit`.
    const auto first_hit = [&](const glm::vec3& direction, const float limit) {
        std::optional<float> nearest = standing_hit(view.scene, eye, direction, limit);
        if (const std::optional<glm::vec3> ground = ground_hit(area, eye, direction, tuning, scale)) {
            const float t = glm::distance(eye, *ground);
            if (t <= limit && (!nearest || t < *nearest)) {
                nearest = t;
            }
        }
        return nearest;
    };

    rangefinder_reading reading;
    const float ground = terrain_height(area, player);
    const auto read = [&](const glm::vec3& point) {
        reading.distance_meters = horizontal_distance(player, point) * scale.meters_per_world_unit;
        reading.height_meters = (point.y - ground) * scale.meters_per_world_unit;
    };

    // The flag nearest the reticle that it can see: nothing between the
    // eye and its cloth near the top of the stick.
    std::optional<glm::vec3> locked;
    float locked_angle = glm::radians(tuning.flag_lock_degrees);
    for (const glm::vec3& flag : view.flags) {
        if (horizontal_distance(player, flag) * scale.meters_per_world_unit > tuning.max_range_meters) {
            continue;
        }
        const glm::vec3 to_stick = safe_normalize(nearest_on_flagstick(eye, look, flag, view.flag_height) - eye, look);
        const float angle = std::acos(std::clamp(glm::dot(look, to_stick), -1.0f, 1.0f));
        if (angle > locked_angle) {
            continue;
        }
        const glm::vec3 cloth = flag + world_up * (view.flag_height * flag_cloth_share);
        const float cloth_distance = glm::distance(eye, cloth);
        const std::optional<float> blocked =
            first_hit(safe_normalize(cloth - eye, look), cloth_distance * (1.0f - flag_sight_slack));
        if (!blocked) {
            locked = flag;
            locked_angle = angle;
        }
    }
    if (locked) {
        read(*locked);
        reading.flag = locked;
        return reading;
    }
    if (const std::optional<float> t = first_hit(look, range)) {
        read(eye + look * *t);
    }
    return reading;
}

rangefinder_reading weathered_reading(const rangefinder_reading& clean, const weather_state& weather,
                                      const weather_tuning& weather_rules, const rangefinder_tuning& tuning,
                                      const std::uint32_t ping) {
    const weather_kind rain = rain_kind(weather_rules, weather.rain);
    const bool blind = rain == weather_kind::rain || rain == weather_kind::heavy_rain ||
        fog_kind(weather_rules, weather.fog) == weather_kind::fog;
    if (!blind) {
        const auto share = [](const float amount, const float full) { return full > 0.0f ? std::min(1.0f, amount / full) : 0.0f; };
        const float error = std::max(tuning.light_rain_error * share(weather.rain, weather_rules.light_rain),
                                     tuning.light_fog_error * share(weather.fog, weather_rules.light_fog));
        rangefinder_reading reading = clean;
        if (reading.distance_meters) {
            *reading.distance_meters *= 1.0f + error * (2.0f * ping_roll(ping, 0) - 1.0f);
        }
        if (reading.height_meters) {
            *reading.height_meters *= 1.0f + error * (2.0f * ping_roll(ping, 1) - 1.0f);
        }
        return reading;
    }
    rangefinder_reading nonsense;
    if (ping_roll(ping, 2) < tuning.blank_share) {
        return nonsense;
    }
    const float near = ping_roll(ping, 3);
    const float distance = tuning.max_range_meters * near * near;
    nonsense.distance_meters = distance;
    nonsense.height_meters = distance * tuning.nonsense_slope * (2.0f * ping_roll(ping, 4) - 1.0f);
    return nonsense;
}

bool rangefinder_raised(const game_state& state, const game_input& input) {
    return input.rangefinder_held && state.mode == game_mode::walking && on_foot(state) &&
        owns_rangefinder(active_progress(state));
}

void update_rangefinder(game_state& state, const game_input& input, const float dt) {
    state.rangefinder_active = rangefinder_raised(state, input);
    const float raise = state.tuning.rangefinder.raise_seconds;
    const float change = raise > 0.0f ? dt / raise : 1.0f;
    state.rangefinder_zoom = clamp01(state.rangefinder_zoom + (state.rangefinder_active ? change : -change));
    state.rangefinder = rangefinder_reading{};
    if (state.rangefinder_active) {
        const rangefinder_tuning& tuning = state.tuning.rangefinder;
        state.rangefinder_ping_left -= dt;
        if (state.rangefinder_ping_left <= 0.0f) {
            ++state.rangefinder_ping;
            state.rangefinder_ping_left = std::max(0.0f, state.rangefinder_ping_left + tuning.ping_seconds);
        }
        const rangefinder_reading clean = read_rangefinder(state.area, rangefinder_view_of(state), state.player.position,
                                                           first_person_eye(state), first_person_look(state), tuning,
                                                           state.tuning.scale);
        state.rangefinder = weathered_reading(clean, state.weather, state.tuning.weather, tuning, state.rangefinder_ping);
    }
    state.rangefinder_locked_seconds = state.rangefinder.flag ? state.rangefinder_locked_seconds + dt : 0.0f;
}

bool rangefinder_lock_lit(const game_state& state) {
    const rangefinder_tuning& tuning = state.tuning.rangefinder;
    const float seconds = state.rangefinder_locked_seconds;
    if (!state.rangefinder.flag || seconds >= tuning.jolt_seconds || tuning.jolt_flashes <= 0) {
        return state.rangefinder.flag.has_value();
    }
    const float flash = tuning.jolt_seconds / static_cast<float>(tuning.jolt_flashes);
    return std::fmod(seconds, flash) < flash * 0.5f;
}

float rangefinder_magnification(const game_state& state) {
    return 1.0f + (std::max(1.0f, state.tuning.rangefinder.magnification) - 1.0f) * state.rangefinder_zoom;
}

int rounded_rangefinder_meters(const float distance_meters) {
    return static_cast<int>(std::floor(std::max(0.0f, distance_meters) + 0.5f));
}
