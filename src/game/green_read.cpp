#include "game/green_read.h"

#include "game/game_state.h"
#include "game/golf_cart.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/geometric.hpp>

namespace {
// Where in its slide a lattice point's dot starts, 0..1, so neighbours
// don't slide in step.
float slide_offset(const int x, const int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(z) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xffffu) / 65536.0f;
}
}

bool crouch_held(const game_state& state, const game_input& input) {
    return input.crouch_held && state.mode == game_mode::walking && on_foot(state) && state.player.vertical.grounded;
}

bool reading_green(const game_state& state) {
    return state.green_read_seconds > 0.0f;
}

void update_crouch(game_state& state, const game_input& input, const float dt) {
    const bool held = crouch_held(state, input);
    const float seconds = state.tuning.green_read.crouch_seconds;
    const float change = seconds > 0.0f ? dt / seconds : 1.0f;
    state.crouch = clamp01(state.crouch + (held ? change : -change));
    state.green_read_seconds = held ? state.green_read_seconds + std::max(0.0f, dt) : 0.0f;
}

float my_eye_height(const game_state& state) {
    const float standing = state.tuning.camera.walking_eye_height;
    return standing + (state.tuning.green_read.eye_height - standing) * state.crouch;
}

std::vector<green_read_dot> green_read_dots(const play_area& area,
                                            const glm::vec3& center,
                                            const float seconds,
                                            const std::vector<glm::vec3>& keep_clear,
                                            const green_read_tuning& tuning,
                                            const world_scale_tuning& scale) {
    std::vector<green_read_dot> dots;
    const float units = 1.0f / std::max(0.0001f, scale.meters_per_world_unit);
    const float spacing = tuning.spacing_meters * units;
    const float radius = tuning.radius_meters * units;
    if (spacing <= 0.0f || radius <= 0.0f) {
        return dots;
    }
    const float clear = tuning.clear_meters * units;
    const float edge = std::max(0.0001f, tuning.edge_fade_meters * units);
    const int reach = static_cast<int>(std::ceil(radius / spacing));
    const int cx = static_cast<int>(std::floor(center.x / spacing + 0.5f));
    const int cz = static_cast<int>(std::floor(center.z / spacing + 0.5f));
    for (int x = cx - reach; x <= cx + reach; ++x) {
        for (int z = cz - reach; z <= cz + reach; ++z) {
            const glm::vec3 at(static_cast<float>(x) * spacing, center.y, static_cast<float>(z) * spacing);
            const float from_center = horizontal_distance(at, center);
            if (from_center > radius) {
                continue;
            }
            const terrain_sample ground = sample_area(area, at);
            if (ground.material != terrain_material::green) {
                continue;
            }
            const glm::vec3 point = ground.point;
            const bool in_the_way = std::any_of(keep_clear.begin(), keep_clear.end(), [&](const glm::vec3& spot) {
                return horizontal_distance(point, spot) < clear;
            });
            if (in_the_way) {
                continue;
            }
            const float edge_alpha = clamp01((radius - from_center) / edge);
            const glm::vec3 tilt = horizontal(ground.normal);
            const float slope = glm::length(tilt) / std::max(0.0001f, ground.normal.y);
            const float percent = slope * 100.0f;
            if (percent < tuning.level_percent || tuning.drift_meters <= 0.0f) {
                dots.push_back(green_read_dot{point, edge_alpha});
                continue;
            }
            const float cycle = tuning.drift_meters / std::max(0.0001f, percent * tuning.drift_speed_per_percent);
            const float phase = seconds / cycle + slide_offset(x, z);
            const float along = phase - std::floor(phase);
            const float slid = tuning.drift_meters * units * along;
            const glm::vec3 downhill = tilt / glm::length(tilt);
            const glm::vec3 position = point + downhill * slid - glm::vec3(0.0f, slope * slid, 0.0f);
            dots.push_back(green_read_dot{position, edge_alpha * 4.0f * along * (1.0f - along)});
        }
    }
    return dots;
}
