#include "renderer/water_crop.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace {
// Mirrors water.frag: reflected_color, ripple_normal and fine_slope_at.
constexpr float tilt_meters_per_meter = 0.08f;
constexpr glm::vec2 smear_spread{0.003f, 0.04f};
constexpr float smear_tap_reach = 0.5f;           // taps lie within half the spread of the centre
constexpr float wave_slope_total = 1.0f;          // the waves[] slope shares add up to one
constexpr float fine_gradient_reach = 1.41421356f / (3.0f * 0.15f);  // value noise is in 0..1; e = 0.15

// A lookup point may land this near the camera plane before the shader's own
// max(w, 0.01) would take over; any nearer and the reach is not bounded.
constexpr float smallest_lookup_depth = 0.02f;
// Planes of the picture's edges are loosened by this share so a cluster that
// only touches the picture is never dropped.
constexpr float edge_slack = 0.002f;
constexpr float rectangle_guard_texels = 1.0f;

struct polygon {
    std::array<glm::vec3, 12> points{};
    int count = 0;
};

float side(const glm::vec4& plane, const glm::vec3& point) {
    return glm::dot(glm::vec3(plane), point) + plane.w;
}

// Keeps the part of `poly` where `plane` is non-negative.
void clip_to(polygon& poly, const glm::vec4& plane) {
    polygon kept;
    for (int i = 0; i < poly.count; ++i) {
        const glm::vec3& a = poly.points[static_cast<std::size_t>(i)];
        const glm::vec3& b = poly.points[static_cast<std::size_t>((i + 1) % poly.count)];
        const float da = side(plane, a);
        const float db = side(plane, b);
        if (da >= 0.0f) { kept.points[static_cast<std::size_t>(kept.count++)] = a; }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            kept.points[static_cast<std::size_t>(kept.count++)] = a + (b - a) * (da / (da - db));
        }
    }
    poly = kept;
}
}

water_lookup_reach water_reflection_reach(const water_style& style, const float wind_meters_per_second, const float rain) {
    const float wind = water_wind_roughness(style, wind_meters_per_second);
    const float rough = style.calm_ripple_share + (1.0f - style.calm_ripple_share) * wind;
    const float rain_share = std::clamp(rain, 0.0f, 1.0f);
    const float slope = rough * (style.ripple_slope * wave_slope_total + style.fine_slope * fine_gradient_reach) +
                        (rain_share > 0.0f ? style.rain_ring_slope : 0.0f);
    const float smear = rough * (style.fine_slope + style.ripple_slope * wave_slope_total) +
                        0.5f * rain_share * style.rain_ring_slope;
    water_lookup_reach reach;
    reach.tilt = tilt_meters_per_meter * slope / std::sqrt(1.0f + slope * slope);
    reach.smear = smear_tap_reach * smear_spread * smear;
    return reach;
}

water_cluster_cover water_cluster_coverage(const water_cluster& cluster, const float level, const glm::mat4& view_proj,
                                           const glm::vec3& eye, const water_lookup_reach& reach) {
    const glm::vec4 x = glm::row(view_proj, 0);
    const glm::vec4 y = glm::row(view_proj, 1);
    const glm::vec4 z = glm::row(view_proj, 2);
    const glm::vec4 w = glm::row(view_proj, 3);
    const float loose = 1.0f + edge_slack;
    polygon poly;
    for (const glm::vec2 corner : {glm::vec2(cluster.min.x, cluster.min.z), glm::vec2(cluster.max.x, cluster.min.z),
                                   glm::vec2(cluster.max.x, cluster.max.z), glm::vec2(cluster.min.x, cluster.max.z)}) {
        poly.points[static_cast<std::size_t>(poly.count++)] = glm::vec3(corner.x, level, corner.y);
    }
    for (const glm::vec4& plane : {x + loose * w, loose * w - x, y + loose * w, loose * w - y, z + w}) {
        clip_to(poly, plane);
        if (poly.count == 0) { return {}; }
    }
    float farthest = 0.0f;
    for (int i = 0; i < poly.count; ++i) { farthest = std::max(farthest, glm::distance(eye, poly.points[static_cast<std::size_t>(i)])); }
    const float shift = reach.tilt * farthest * 1.02f + 0.001f;

    // The lookups of the polygon's points lie within the polygon grown by
    // `shift` each way along the surface: the hull of its corners moved by
    // (+-shift, +-shift), which projects to the box of its projected corners
    // while every one is in front of the camera.
    glm::vec2 low(1.0e30f);
    glm::vec2 high(-1.0e30f);
    for (int i = 0; i < poly.count; ++i) {
        for (const glm::vec2 way : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)}) {
            const glm::vec3 point = poly.points[static_cast<std::size_t>(i)] + glm::vec3(way.x, 0.0f, way.y) * shift;
            const glm::vec4 clip = view_proj * glm::vec4(point, 1.0f);
            if (clip.w < smallest_lookup_depth) { return {water_cover::rectangle, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)}; }
            const glm::vec2 uv = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
            low = glm::min(low, uv);
            high = glm::max(high, uv);
        }
    }
    low = glm::clamp(low - reach.smear, glm::vec2(0.0f), glm::vec2(1.0f));
    high = glm::clamp(high + reach.smear, glm::vec2(0.0f), glm::vec2(1.0f));
    return {water_cover::rectangle, glm::vec4(low, high)};
}

reflection_crop snap_reflection_crop(const glm::vec4& rectangle, const int full_width, const int full_height) {
    const auto span = [](const float low, const float high, const int full, int& first, int& size) {
        // Bilinear taps at s read texels floor(s - 0.5) and the next one.
        const float guard = rectangle_guard_texels;
        first = std::clamp(static_cast<int>(std::floor(low * static_cast<float>(full) - 0.5f - guard)), 0, full - 1);
        const int last = std::clamp(static_cast<int>(std::floor(high * static_cast<float>(full) - 0.5f + guard)) + 1, 0, full - 1);
        size = std::max(last, first) - first + 1;
    };
    reflection_crop crop;
    crop.full_width = std::max(1, full_width);
    crop.full_height = std::max(1, full_height);
    span(rectangle.x, rectangle.z, crop.full_width, crop.x, crop.width);
    span(rectangle.y, rectangle.w, crop.full_height, crop.y, crop.height);
    return crop;
}

glm::mat4 reflection_crop_matrix(const reflection_crop& crop) {
    glm::mat4 matrix(1.0f);
    const float width = static_cast<float>(crop.width);
    const float height = static_cast<float>(crop.height);
    const float full_width = static_cast<float>(crop.full_width);
    const float full_height = static_cast<float>(crop.full_height);
    matrix[0][0] = full_width / width;
    matrix[1][1] = full_height / height;
    matrix[3][0] = (full_width - 2.0f * static_cast<float>(crop.x) - width) / width;
    matrix[3][1] = (full_height - 2.0f * static_cast<float>(crop.y) - height) / height;
    return matrix;
}

glm::mat4 reflection_texture_matrix(const reflection_crop& crop) {
    glm::mat4 matrix(1.0f);
    const float width_share = static_cast<float>(crop.width) / static_cast<float>(crop.full_width);
    const float height_share = static_cast<float>(crop.height) / static_cast<float>(crop.full_height);
    matrix[0][0] = width_share;
    matrix[1][1] = height_share;
    matrix[3][0] = width_share - 1.0f;
    matrix[3][1] = height_share - 1.0f;
    return matrix;
}

glm::vec4 reflection_lookup_range(const reflection_crop& crop) {
    const float full_width = static_cast<float>(crop.full_width);
    const float full_height = static_cast<float>(crop.full_height);
    return {(0.5f - static_cast<float>(crop.x)) / full_width, (0.5f - static_cast<float>(crop.y)) / full_height,
            (full_width - 0.5f - static_cast<float>(crop.x)) / full_width,
            (full_height - 0.5f - static_cast<float>(crop.y)) / full_height};
}
