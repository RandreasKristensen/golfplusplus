#include "doctest.h"

#include "renderer/lamp_shadows.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/scattered_light.h"
#include "renderer/scene_haze.h"
#include "renderer/scene_lights.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {
const glm::vec3 eye(0.0f, 1.5f, 0.0f);
constexpr float near_meters = 0.3f;
constexpr int map_resolution = 128;

// A floodlight's lamp at `position` shining along `direction`, its cone 40°
// to the edge, full 10° in, reaching 65 m.
render_light floodlight_at(const glm::vec3& position, const glm::vec3& direction) {
    render_light light;
    light.position = position;
    light.color = glm::vec3(1.25f);
    light.radius = 65.0f;
    light.direction = direction;
    light.cone_edge_cos = std::cos(glm::radians(40.0f));
    light.cone_full_cos = std::cos(glm::radians(10.0f));
    light.fixed_lamp = true;
    return light;
}

// 15 m up, 30 m ahead of the eye, shining straight down.
render_light overhead_floodlight() {
    return floodlight_at(glm::vec3(0.0f, 15.0f, 30.0f), glm::vec3(0.0f, -1.0f, 0.0f));
}

// A cart's headlight: as bright, but it moves.
render_light headlight_at(const glm::vec3& position) {
    render_light light = floodlight_at(position, glm::vec3(0.0f, 0.0f, 1.0f));
    light.fixed_lamp = false;
    return light;
}

// Fog round the eye, with no haze.
frame_lighting_block foggy_night(const std::vector<render_light>& lights) {
    render_lighting lighting;
    lighting.weather.scatter_per_meter = 0.02f;
    lighting.weather.scatter_forwardness = 0.85f;
    lighting.beam_near_share = 0.2f;
    lighting.beam_core_meters = 0.5f;
    scene_haze haze;
    haze.full_distance = 1000.0f;
    haze.eye = eye;
    return make_frame_lighting_block(lighting, haze, lights);
}

float brightness(const glm::vec3& light) {
    return light.r + light.g + light.b;
}

// A tree trunk 0.4 m round and 10 m tall standing at `foot`.
struct trunk {
    glm::vec3 foot{0.0f};
    float radius = 0.4f;
    float height = 10.0f;
};

// The first point along `from` + t `way` (t in 0..1) inside the trunk.
std::optional<glm::vec3> first_hit(const trunk& tree, const glm::vec3& from, const glm::vec3& way) {
    std::optional<float> nearest;
    const auto keep = [&](const float t) {
        const glm::vec3 at = from + way * t;
        const float across = glm::length(glm::vec2(at.x - tree.foot.x, at.z - tree.foot.z));
        if (t >= 0.0f && t <= 1.0f && at.y >= tree.foot.y - 1.0e-4f && at.y <= tree.foot.y + tree.height + 1.0e-4f &&
            across <= tree.radius + 1.0e-4f && (!nearest || t < *nearest)) {
            nearest = t;
        }
    };
    const glm::vec2 start(from.x - tree.foot.x, from.z - tree.foot.z);
    const glm::vec2 level(way.x, way.z);
    const float a = glm::dot(level, level);
    const float b = 2.0f * glm::dot(start, level);
    const float c = glm::dot(start, start) - tree.radius * tree.radius;
    const float roots = b * b - 4.0f * a * c;
    if (a > 0.0f && roots >= 0.0f) {
        keep((-b - std::sqrt(roots)) / (2.0f * a));
        keep((-b + std::sqrt(roots)) / (2.0f * a));
    }
    if (std::abs(way.y) > 0.0f) {
        keep((tree.foot.y + tree.height - from.y) / way.y);
    }
    return nearest ? std::optional<glm::vec3>(from + way * *nearest) : std::nullopt;
}

// A lamp's shadow map of one trunk as lamp_shadow_pass draws it, and read
// as sun_shafts.frag's lamp_light_once reads it, in one tap.
class trunk_map {
public:
    trunk_map(const render_light& lamp, const trunk& tree)
        : view_proj_(lamp_shadow_view_proj(view_of(lamp), near_meters)), radius_(lamp.radius) {
        const glm::mat4 inverse = glm::inverse(view_proj_);
        for (int y = 0; y < map_resolution; ++y) {
            for (int x = 0; x < map_resolution; ++x) {
                const glm::vec2 ndc = (glm::vec2(x, y) + 0.5f) / static_cast<float>(map_resolution) * 2.0f - 1.0f;
                const glm::vec4 near_point = inverse * glm::vec4(ndc, -1.0f, 1.0f);
                const glm::vec4 far_point = inverse * glm::vec4(ndc, 1.0f, 1.0f);
                const glm::vec3 from = glm::vec3(near_point) / near_point.w;
                const std::optional<glm::vec3> hit = first_hit(tree, from, glm::vec3(far_point) / far_point.w - from);
                depth_[static_cast<std::size_t>(y * map_resolution + x)] = hit ? window_depth(*hit) : 1.0f;
            }
        }
    }

    float lit_at(const glm::vec3& point) const {
        const glm::vec4 clip = view_proj_ * glm::vec4(point, 1.0f);
        if (clip.w <= 0.0f) {
            return 1.0f;
        }
        const glm::vec3 coords = glm::vec3(clip) / clip.w * 0.5f + 0.5f;
        if (glm::any(glm::lessThan(coords, glm::vec3(0.0f))) || glm::any(glm::greaterThan(coords, glm::vec3(1.0f)))) {
            return 1.0f;
        }
        const glm::ivec2 texel = glm::min(glm::ivec2(glm::vec2(coords) * static_cast<float>(map_resolution)),
                                          glm::ivec2(map_resolution - 1));
        // lamp_shadow_pass's bias: 5 cm along the beam, in window depth.
        const float bias = 0.05f * radius_ * near_meters / (radius_ - near_meters) / (clip.w * clip.w);
        return coords.z - bias <= depth_[static_cast<std::size_t>(texel.y * map_resolution + texel.x)] ? 1.0f : 0.0f;
    }

private:
    float window_depth(const glm::vec3& world) const {
        const glm::vec4 clip = view_proj_ * glm::vec4(world, 1.0f);
        return clip.z / clip.w * 0.5f + 0.5f;
    }

    glm::mat4 view_proj_;
    float radius_ = 0.0f;
    std::array<float, map_resolution * map_resolution> depth_{};
};

// How bright the beam shows looking from the eye at the ground at `ground`,
// once the shaft pass has taken out what `map` shades.
float beam_seen(const frame_lighting_block& block, const trunk_map& map, const glm::vec3& ground) {
    const glm::vec3 way = glm::normalize(ground - eye);
    const float meters = glm::distance(ground, eye);
    const auto lit = [&map](const glm::vec3& point) { return map.lit_at(point); };
    return brightness(scattered_from(block, 0, way, meters) - lamp_shaft_shade(block, 0, lit, way, meters, 16, 0.5f));
}
}

TEST_CASE("a trunk between a floodlight and the eye leaves a dark stripe in its beam in the fog, and none on the lamp's far side") {
    const render_light lamp = overhead_floodlight();
    const frame_lighting_block fog = foggy_night({lamp});
    // Looks at the ground just short of a trunk 6 m from the lamp's foot
    // towards the eye: their last few metres run through the beam, under
    // the lamp's light past the trunk.
    const trunk_map between(lamp, trunk{glm::vec3(0.0f, 0.0f, 24.0f)});
    const std::array<float, 5> across{-3.0f, -1.5f, 0.0f, 1.5f, 3.0f};
    std::array<float, 5> seen{};
    std::array<float, 5> unshaded{};
    for (std::size_t k = 0; k < across.size(); ++k) {
        const glm::vec3 ground(across[k], 0.0f, 21.0f);
        seen[k] = beam_seen(fog, between, ground);
        unshaded[k] = brightness(scattered_from(fog, 0, glm::normalize(ground - eye), glm::distance(ground, eye)));
        CHECK(unshaded[k] > 0.0f);
    }
    // In line with the trunk the beam is dark; beside it, as bright as it was.
    CHECK(seen[2] < 0.1f * unshaded[2]);
    for (const std::size_t side : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{4}}) {
        CHECK(seen[side] > 0.9f * unshaded[side]);
        CHECK(seen[side] > 5.0f * seen[2]);
    }

    // The same trunk on the lamp's far side shades air away from the eye,
    // never these looks.
    const trunk_map beyond(lamp, trunk{glm::vec3(0.0f, 0.0f, 36.0f)});
    for (std::size_t k = 0; k < across.size(); ++k) {
        CHECK(near(beam_seen(fog, beyond, glm::vec3(across[k], 0.0f, 21.0f)), unshaded[k], unshaded[k] * 1.0e-4f));
    }
}

TEST_CASE("a beam all in shadow loses all the light it scattered, and one all lit none") {
    const frame_lighting_block fog = foggy_night({overhead_floodlight()});
    const auto dark = [](const glm::vec3&) { return 0.0f; };
    const auto bright = [](const glm::vec3&) { return 1.0f; };
    const glm::vec3 way = glm::normalize(glm::vec3(0.0f, -1.5f, 60.0f));
    const glm::vec3 whole = scattered_from(fog, 0, way, 60.0f);
    CHECK(brightness(whole) > 0.0f);
    for (const float offset : {0.03f, 0.5f, 0.97f}) {
        CHECK(near(lamp_shaft_shade(fog, 0, dark, way, 60.0f, 16, offset), whole, brightness(whole) * 1.0e-5f));
        CHECK(brightness(lamp_shaft_shade(fog, 0, bright, way, 60.0f, 16, offset)) == 0.0f);
    }
    // What the scene was drawn with is the lights' scattered_from summed.
    CHECK(near(scattered_light(fog, eye + way * 60.0f), whole, brightness(whole) * 1.0e-5f));
}

TEST_CASE("a floodlight's map looks down its beam from past its head, across its cone, out to its reach") {
    const render_light lamp = floodlight_at(glm::vec3(5.0f, 11.0f, -3.0f), glm::normalize(glm::vec3(0.4f, -1.0f, 0.2f)));
    const glm::mat4 view_proj = lamp_shadow_view_proj(view_of(lamp), near_meters);
    const auto coords = [&view_proj](const glm::vec3& world) {
        const glm::vec4 clip = view_proj * glm::vec4(world, 1.0f);
        return glm::vec3(clip) / clip.w * 0.5f + 0.5f;
    };
    // Down the beam: the map's middle, further along deeper.
    const glm::vec3 near_on_axis = coords(lamp.position + lamp.direction * 5.0f);
    const glm::vec3 far_on_axis = coords(lamp.position + lamp.direction * 40.0f);
    CHECK(glm::distance(glm::vec2(near_on_axis), glm::vec2(0.5f)) < 1.0e-4f);
    CHECK(glm::distance(glm::vec2(far_on_axis), glm::vec2(0.5f)) < 1.0e-4f);
    CHECK(far_on_axis.z > near_on_axis.z);
    CHECK(far_on_axis.z < 1.0f);
    // The lamp's own head, a few centimetres in front of it, casts nothing.
    CHECK(coords(lamp.position + lamp.direction * 0.2f).z < 0.0f);
    // Past its reach is past the map.
    CHECK(coords(lamp.position + lamp.direction * 70.0f).z > 1.0f);
    // The cone's edge is just inside the map, every way round.
    const glm::vec3 side = glm::normalize(glm::cross(lamp.direction, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 other = glm::cross(side, lamp.direction);
    const float edge = std::acos(lamp.cone_edge_cos) * 0.99f;
    for (const glm::vec3& out : {side, -side, other, -other}) {
        const glm::vec3 at = coords(lamp.position + (lamp.direction * std::cos(edge) + out * std::sin(edge)) * 20.0f);
        CHECK(at.x >= 0.0f);
        CHECK(at.x <= 1.0f);
        CHECK(at.y >= 0.0f);
        CHECK(at.y <= 1.0f);
    }
    // A lamp pointing straight down still has a map, the ground under it in the middle.
    const glm::vec4 under = lamp_shadow_view_proj(view_of(overhead_floodlight()), near_meters) * glm::vec4(0.0f, 5.0f, 30.0f, 1.0f);
    CHECK(std::isfinite(under.x));
    CHECK(glm::length(glm::vec2(under) / under.w) < 1.0e-4f);
}

TEST_CASE("only the floodlights nearest the camera whose beams the shafts reach take shadows, never a headlight") {
    const std::vector<render_light> lights{
        floodlight_at(eye + glm::vec3(0.0f, 10.0f, 300.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        headlight_at(eye + glm::vec3(0.0f, 0.0f, 5.0f)),
        floodlight_at(eye + glm::vec3(0.0f, 10.0f, 60.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        floodlight_at(eye + glm::vec3(0.0f, 10.0f, 20.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        floodlight_at(eye + glm::vec3(0.0f, 10.0f, 120.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
    };
    const std::vector<render_light> scene = nearest_scene_lights(lights, eye);
    const std::vector<std::size_t> chosen = shadowed_lamps(scene, eye, 160.0f, 2);
    REQUIRE(chosen.size() == 2);
    for (const std::size_t i : chosen) {
        CHECK(scene[i].fixed_lamp);
    }
    CHECK(near(scene[chosen[0]].position.z, eye.z + 20.0f));
    CHECK(near(scene[chosen[1]].position.z, eye.z + 60.0f));
    // With room for all, the one 300 m off is still out of the shafts' reach.
    const std::vector<std::size_t> all = shadowed_lamps(scene, eye, 160.0f, 8);
    CHECK(all.size() == 3);
    CHECK(shadowed_lamps(scene, eye, 160.0f, 0).empty());
}

TEST_CASE("a floodlight's map stays in its tile while it stays near, so walking redraws nothing until a new lamp comes near") {
    const render_light first = floodlight_at(glm::vec3(0.0f, 11.0f, 20.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    const render_light second = floodlight_at(glm::vec3(0.0f, 11.0f, 80.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    const render_light third = floodlight_at(glm::vec3(0.0f, 11.0f, 140.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    const std::vector<std::optional<lamp_view>> empty(2);

    const lamp_tiling start = place_lamps(empty, {first, second}, {0, 1});
    CHECK(start.redraw.size() == 2);
    REQUIRE(start.slots.size() == 2);

    // The camera walks on: the same two lamps, now nearest the other way round.
    const lamp_tiling walked = place_lamps(start.held, {second, first}, {0, 1});
    CHECK(walked.redraw.empty());
    REQUIRE(walked.slots.size() == 2);
    CHECK(walked.slots[0].tile == start.slots[1].tile);
    CHECK(walked.slots[1].tile == start.slots[0].tile);
    CHECK(walked.slots[0].light == 0);

    // A new lamp comes near and the first drops out: only its tile is redrawn.
    const lamp_tiling onward = place_lamps(walked.held, {second, third, first}, {0, 1});
    REQUIRE(onward.redraw.size() == 1);
    CHECK(onward.redraw[0] == start.slots[0].tile);
    CHECK(onward.slots[0].tile == start.slots[1].tile);
    CHECK(onward.slots[1].tile == start.slots[0].tile);
    CHECK(*onward.held[onward.slots[1].tile] == view_of(third));
}
