#include "renderer/lamp_shadows.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
// The widest a map looks: a cone near a half-space is not shadowed.
constexpr float max_view_deg = 170.0f;
}

bool lamp_view::operator==(const lamp_view& other) const {
    return position == other.position && direction == other.direction && cone_edge_cos == other.cone_edge_cos &&
        radius == other.radius;
}

lamp_view view_of(const render_light& light) {
    return lamp_view{light.position, light.direction, light.cone_edge_cos, light.radius};
}

std::vector<std::size_t> shadowed_lamps(const std::vector<render_light>& lights, const glm::vec3& eye,
                                        const float reach_meters, const std::size_t count) {
    const float narrowest_edge_cos = std::cos(glm::radians(max_view_deg * 0.5f));
    std::vector<std::size_t> chosen;
    for (std::size_t i = 0; i < lights.size() && chosen.size() < count; ++i) {
        const render_light& light = lights[i];
        if (light.fixed_lamp && light.radius > 0.0f && glm::dot(light.direction, light.direction) > 0.5f &&
            light.cone_edge_cos > narrowest_edge_cos &&
            glm::distance(light.position, eye) - light.radius <= reach_meters) {
            chosen.push_back(i);
        }
    }
    return chosen;
}

glm::mat4 lamp_shadow_view_proj(const lamp_view& lamp, const float near_meters) {
    const glm::vec3 way = glm::normalize(lamp.direction);
    const glm::vec3 up = std::abs(way.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const float across = 2.0f * std::acos(std::clamp(lamp.cone_edge_cos, -1.0f, 1.0f));
    const float view = std::min(across, glm::radians(max_view_deg));
    return glm::perspective(view, 1.0f, near_meters, std::max(lamp.radius, near_meters * 2.0f)) *
        glm::lookAt(lamp.position, lamp.position + way, up);
}

lamp_tiling place_lamps(const std::vector<std::optional<lamp_view>>& held, const std::vector<render_light>& lights,
                        const std::vector<std::size_t>& chosen) {
    lamp_tiling out;
    out.held = held;
    std::vector<bool> taken(held.size(), false);
    std::vector<std::optional<std::size_t>> tile_of(chosen.size());
    for (std::size_t c = 0; c < chosen.size(); ++c) {
        const lamp_view view = view_of(lights[chosen[c]]);
        for (std::size_t t = 0; t < held.size(); ++t) {
            if (!taken[t] && held[t] && *held[t] == view) {
                tile_of[c] = t;
                taken[t] = true;
                break;
            }
        }
    }
    const auto free_tile = [&]() -> std::optional<std::size_t> {
        for (const bool want_empty : {true, false}) {
            for (std::size_t t = 0; t < held.size(); ++t) {
                if (!taken[t] && (!want_empty || !out.held[t])) {
                    return t;
                }
            }
        }
        return std::nullopt;
    };
    for (std::size_t c = 0; c < chosen.size(); ++c) {
        if (!tile_of[c]) {
            tile_of[c] = free_tile();
            if (!tile_of[c]) {
                continue;
            }
            taken[*tile_of[c]] = true;
            out.held[*tile_of[c]] = view_of(lights[chosen[c]]);
            out.redraw.push_back(*tile_of[c]);
        }
        out.slots.push_back(lamp_slot{chosen[c], *tile_of[c]});
    }
    return out;
}
