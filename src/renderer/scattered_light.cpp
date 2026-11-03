#include "renderer/scattered_light.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace {
// lighting.glsl's gauss_at and gauss_weight.
constexpr std::array<float, 3> gauss_at{0.1127016654f, 0.5f, 0.8872983346f};
constexpr std::array<float, 3> gauss_weight{0.2777777778f, 0.4444444444f, 0.2777777778f};

// As the GPU's: an empty light's equal edges give 0 / 0, which its min and
// max (as fmin and fmax) drop.
float smoothstep(const float edge0, const float edge1, const float x) {
    const float t = std::fmin(std::fmax((x - edge0) / (edge1 - edge0), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec2 through_reach(const frame_lighting_block& block, const std::size_t i, const glm::vec3& way, const float far) {
    const glm::vec3 from_light = glm::vec3(block.haze_eye) - glm::vec3(block.light_position[i]);
    const float nearest = -glm::dot(from_light, way);
    const float apart = glm::dot(from_light, from_light) - nearest * nearest;
    const float radius = block.light_position[i].w;
    const float half_chord = std::sqrt(std::max(radius * radius - apart, 0.0f));
    return {std::max(nearest - half_chord, 0.0f), std::min(nearest + half_chord, far)};
}

bool in_cone(const frame_lighting_block& block, const std::size_t i, const glm::vec3& world) {
    const glm::vec3 out_of = world - glm::vec3(block.light_position[i]);
    return glm::dot(out_of, glm::vec3(block.light_cone[i])) >= block.light_cone[i].w * glm::length(out_of);
}

glm::vec2 through_cone(const frame_lighting_block& block, const std::size_t i, const glm::vec3& way, const glm::vec2 span) {
    const glm::vec3 axis(block.light_cone[i]);
    const float edge = block.light_cone[i].w;
    if (span.x >= span.y || glm::dot(axis, axis) < 0.5f || edge <= 0.0f) {
        return span;
    }
    const glm::vec3 eye(block.haze_eye);
    const glm::vec3 from_light = eye - glm::vec3(block.light_position[i]);
    const float way_along = glm::dot(way, axis);
    const float from_along = glm::dot(from_light, axis);
    const float a = way_along * way_along - edge * edge;
    const float b = 2.0f * (way_along * from_along - edge * edge * glm::dot(way, from_light));
    const float c = from_along * from_along - edge * edge * glm::dot(from_light, from_light);
    float first = span.x;
    float second = span.x;
    const float roots = b * b - 4.0f * a * c;
    if (std::abs(a) > 1.0e-6f && roots >= 0.0f) {
        const float root = std::sqrt(roots);
        const float one = (-b - root) / (2.0f * a);
        const float other = (-b + root) / (2.0f * a);
        first = std::clamp(std::min(one, other), span.x, span.y);
        second = std::clamp(std::max(one, other), span.x, span.y);
    } else if (std::abs(a) <= 1.0e-6f && std::abs(b) > 1.0e-6f) {
        first = std::clamp(-c / b, span.x, span.y);
        second = first;
    }
    glm::vec2 inside(span.y, span.x);
    const std::array<float, 3> cuts{span.x, first, second};
    const std::array<float, 3> ends{first, second, span.y};
    for (std::size_t k = 0; k < cuts.size(); ++k) {
        if (ends[k] > cuts[k] && in_cone(block, i, eye + way * (0.5f * (cuts[k] + ends[k])))) {
            inside = glm::vec2(std::min(inside.x, cuts[k]), std::max(inside.y, ends[k]));
        }
    }
    return inside;
}

// lighting.glsl's beam_run: the run of the line inside light i's reach and
// cone, as the lamp sees it.
struct beam_run {
    float nearest = 0.0f;  // how far along the line it passes nearest the lamp
    float apart = 0.0f;    // how far from it, taking in the housing
    float first = 0.0f;    // the angles the lamp sees the run's ends under
    float last = 0.0f;
};

beam_run beam_run_of(const frame_lighting_block& block, const std::size_t i, const glm::vec3& way, const float far) {
    const glm::vec2 run = through_cone(block, i, way, through_reach(block, i, way, far));
    const glm::vec3 from_light = glm::vec3(block.haze_eye) - glm::vec3(block.light_position[i]);
    beam_run out;
    out.nearest = -glm::dot(from_light, way);
    out.apart = std::sqrt(std::max(glm::dot(from_light, from_light) - out.nearest * out.nearest, 0.0f) +
                          block.scatter.w * block.scatter.w);
    if (run.y > run.x) {
        out.first = std::atan((run.x - out.nearest) / out.apart);
        out.last = std::atan((run.y - out.nearest) / out.apart);
    }
    return out;
}

glm::vec3 beam_point(const frame_lighting_block& block, const beam_run& run, const glm::vec3& way, const float along) {
    return glm::vec3(block.haze_eye) + way * (run.nearest + run.apart * std::tan(run.first + (run.last - run.first) * along));
}
}

float mist_at(const frame_lighting_block& block, const glm::vec3& world) {
    return block.mist.z * std::exp((block.mist.x - world.y) * block.mist.y);
}

float mist_depth(const frame_lighting_block& block, const glm::vec3& way, const float meters) {
    const float run = std::min(meters, 1.0f / block.haze_eye.w);
    const float rise = way.y * run * block.mist.y;
    const float from_eye = (block.mist.x - block.haze_eye.y) * block.mist.y;
    const float along = std::abs(rise) < 0.01f ? std::exp(from_eye) * (1.0f - rise * (0.5f - rise / 6.0f))
                                               : (std::exp(from_eye) - std::exp(std::min(from_eye - rise, 60.0f))) / rise;
    return block.mist.z * run * along;
}

float haze_over(const frame_lighting_block& block, const glm::vec3& way, const float meters) {
    const float far = std::min(meters * block.haze_eye.w, 1.0f);
    const float haze = block.haze_color.a * glm::mix(far * far, far, block.air.z);
    return 1.0f - (1.0f - haze) * std::exp(-mist_depth(block, way, meters));
}

float haze_at(const frame_lighting_block& block, const glm::vec3& world) {
    const glm::vec3 to = world - glm::vec3(block.haze_eye);
    const float meters = glm::length(to);
    return haze_over(block, to / std::max(meters, 0.001f), meters);
}

glm::vec3 haze_sun_glow(const frame_lighting_block& block, const glm::vec3& way) {
    const float g = block.air.y;
    const float turn = 1.0f + g * g - 2.0f * g * glm::dot(way, glm::vec3(block.sun_direction));
    return glm::vec3(block.sun_color) * (block.air.x * (1.0f - g * g) / (turn * std::sqrt(turn)));
}

glm::vec3 haze_toward(const frame_lighting_block& block, const glm::vec3& way) {
    return glm::vec3(block.haze_color) + haze_sun_glow(block, way);
}

glm::vec3 shaft_shade(const frame_lighting_block& block, const std::function<float(const glm::vec3&)>& lit_at,
                      const glm::vec3& way, const float meters, const int steps, const float offset) {
    const glm::vec3 eye(block.haze_eye);
    const float step = meters / static_cast<float>(std::max(steps, 1));
    float shaded = 0.0f;
    float before = 0.0f;
    for (int k = 0; k < steps; ++k) {
        const float after = haze_over(block, way, step * static_cast<float>(k + 1));
        shaded += (after - before) * (1.0f - lit_at(eye + way * (step * (static_cast<float>(k) + offset))));
        before = after;
    }
    return haze_sun_glow(block, way) * shaded;
}

float shaft_peak(const frame_lighting_block& block, const float reach_meters) {
    const float g = block.air.y;
    const float sun = std::max(block.sun_color.r, std::max(block.sun_color.g, block.sun_color.b));
    // The low mist at its thickest, on the lowest ground, all the way.
    const float far = std::min(reach_meters * block.haze_eye.w, 1.0f);
    const float haze = block.haze_color.a * glm::mix(far * far, far, block.air.z);
    const float most = 1.0f - (1.0f - haze) * std::exp(-block.mist.z * std::min(reach_meters, 1.0f / block.haze_eye.w));
    return sun * block.air.x * (1.0f + g) / std::max((1.0f - g) * (1.0f - g), 1.0e-4f) * most;
}

reaching_light light_reaching(const frame_lighting_block& block, const std::size_t i, const glm::vec3& world) {
    const glm::vec3 to = glm::vec3(block.light_position[i]) - world;
    const float distance_to = glm::length(to);
    reaching_light out;
    out.towards = to / std::max(distance_to, 0.001f);
    const float radius = block.light_position[i].w;
    const float reach = std::clamp(1.0f - distance_to * distance_to / std::max(radius * radius, 0.0001f), 0.0f, 1.0f);
    const float cone = smoothstep(block.light_cone[i].w, block.light_color[i].w, glm::dot(-out.towards, glm::vec3(block.light_cone[i])));
    out.light = glm::vec3(block.light_color[i]) * reach * reach * cone;
    return out;
}

reaching_light air_reaching(const frame_lighting_block& block, const std::size_t i, const glm::vec3& world) {
    reaching_light out = light_reaching(block, i, world);
    const float near = block.scatter.z * block.light_position[i].w;
    const glm::vec3 to = glm::vec3(block.light_position[i]) - world;
    out.light *= near * near / (glm::dot(to, to) + block.scatter.w * block.scatter.w);
    return out;
}

namespace {
// lighting.glsl's beam_air: light i the air at `point` sends along `way`, as
// much as the haze lets through, by the share the air and the mist there scatter.
glm::vec3 beam_air(const frame_lighting_block& block, const std::size_t i, const glm::vec3& point, const glm::vec3& way) {
    const reaching_light reaching = light_reaching(block, i, point);
    const float g = block.scatter.y;
    const float turn = 1.0f + g * g - 2.0f * g * glm::dot(reaching.towards, way);
    return reaching.light * ((block.scatter.x + mist_at(block, point)) * (1.0f - g * g) / (4.0f * turn * std::sqrt(turn)) *
                             (1.0f - haze_at(block, point)));
}
}

glm::vec3 scattered_from(const frame_lighting_block& block, const std::size_t i, const glm::vec3& way, const float far) {
    const beam_run run = beam_run_of(block, i, way, far);
    if (run.last <= run.first) {
        return glm::vec3(0.0f);
    }
    glm::vec3 along(0.0f);
    for (std::size_t k = 0; k < gauss_at.size(); ++k) {
        along += beam_air(block, i, beam_point(block, run, way, gauss_at[k]), way) * gauss_weight[k];
    }
    const float near = block.scatter.z * block.light_position[i].w;
    return along * (near * near * (run.last - run.first) / run.apart);
}

glm::vec3 scattered_light(const frame_lighting_block& block, const glm::vec3& world) {
    const glm::vec3 to = world - glm::vec3(block.haze_eye);
    const float far = glm::length(to);
    const glm::vec3 way = to / std::max(far, 0.001f);
    glm::vec3 sum(0.0f);
    for (std::size_t i = 0; i < block.light_position.size(); ++i) {
        sum += scattered_from(block, i, way, far);
    }
    return sum;
}

glm::vec3 lamp_shaft_shade(const frame_lighting_block& block, const std::size_t i,
                           const std::function<float(const glm::vec3&)>& lit_at, const glm::vec3& way, const float meters,
                           const int steps, const float offset) {
    const beam_run run = beam_run_of(block, i, way, meters);
    if (run.last <= run.first) {
        return glm::vec3(0.0f);
    }
    float all_air = 0.0f;
    float shaded = 0.0f;
    for (int k = 0; k < steps; ++k) {
        const glm::vec3 point = beam_point(block, run, way, (static_cast<float>(k) + offset) / static_cast<float>(steps));
        const glm::vec3 air = beam_air(block, i, point, way);
        const float weight = air.r + air.g + air.b;
        all_air += weight;
        shaded += weight * (1.0f - lit_at(point));
    }
    return all_air > 0.0f ? scattered_from(block, i, way, meters) * (shaded / all_air) : glm::vec3(0.0f);
}
