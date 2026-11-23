#include "doctest.h"

#include "game/content_files.h"
#include "renderer/water_crop.h"
#include "renderer/water_reflection.h"
#include "test_support.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {
water_style shipped_style() {
    const std::optional<lighting_palette> palette = load_lighting(fixture_root());
    return palette ? palette->water : water_style{};
}

// Small deterministic generator, 0 .. 1.
struct random_unit {
    std::uint32_t state = 12345u;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    }
    float between(const float low, const float high) { return low + (high - low) * next(); }
};

struct camera {
    glm::vec3 eye;
    glm::mat4 view_proj;
};

camera random_camera(random_unit& random, const float height_low, const float height_high) {
    camera c;
    c.eye = glm::vec3(random.between(-20, 20), random.between(height_low, height_high), random.between(-20, 20));
    const float yaw = random.between(0.0f, 6.2831853f);
    const float pitch = random.between(-1.4f, 0.3f);
    const glm::vec3 forward(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
    const float fov = random.between(20.0f, 80.0f);
    c.view_proj = glm::perspective(glm::radians(fov), 16.0f / 9.0f, 0.1f, 2000.0f) *
                  glm::lookAt(c.eye, c.eye + forward, glm::vec3(0, 1, 0));
    return c;
}

water_cluster cluster_at(const glm::vec2 low, const glm::vec2 high, const float level) {
    water_cluster cluster;
    cluster.min = glm::vec3(low.x, level, low.y);
    cluster.max = glm::vec3(high.x, level, high.y);
    return cluster;
}

bool contains(const glm::vec4& rectangle, const glm::vec2& uv) {
    constexpr float slack = 1.0e-4f;
    return uv.x >= rectangle.x - slack && uv.x <= rectangle.z + slack && uv.y >= rectangle.y - slack &&
           uv.y <= rectangle.w + slack;
}
}

TEST_CASE("the shaders carry the constants the lookup reach is derived from") {
    const std::optional<std::string> source =
        read_text_file(std::filesystem::path(asset_root()) / "picture" / "shaders" / "water.frag");
    REQUIRE(source.has_value());
    for (const char* text : {"(0.08 * distance(haze_eye.xyz,world))", "vec2 spread = vec2(0.003, 0.04) * smear;",
                             "spread * (float(i) / float(SMEAR_SAMPLES - 1) - 0.5)", "#define SMEAR_SAMPLES 4",
                             "vec3(0.0, 1.0, 0.34), vec3(0.6, 0.63, 0.24), vec3(-0.5, 0.41, 0.18),",
                             "vec3(1.1, 0.26, 0.14), vec3(-1.0, 0.17, 0.10)",
                             "const float e = 0.15;", "value_noise(ground + vec2(e, 0.0)) - value_noise(ground - vec2(e, 0.0))",
                             "/ (3.0 * e)", "clamp(uv + distortion + offset, u_reflection_range.xy, u_reflection_range.zw)",
                             "max(tilted.w,0.01)", "smear += 0.5 * u_rain * u_gusts.z * (1.0 - fine_fade);",
                             "(-sin(3.14159265 * x) * u_gusts.z * (1.0 - age) * near)"}) {
        const bool found = source->find(text) != std::string::npos;
        if (!found) { std::printf("missing from water.frag: %s\n", text); }
        CHECK(found);
    }
}

TEST_CASE("the lookup reach grows with the wind and the rain and stays inside the shader's slopes") {
    const water_style style = shipped_style();
    const water_lookup_reach still = water_reflection_reach(style, 0.0f, 0.0f);
    const water_lookup_reach windy = water_reflection_reach(style, 12.0f, 0.0f);
    const water_lookup_reach rainy = water_reflection_reach(style, 12.0f, 1.0f);
    CHECK(still.tilt > 0.0f);
    CHECK(windy.tilt > still.tilt);
    CHECK(rainy.tilt > windy.tilt);
    CHECK(windy.smear.x > still.smear.x);
    CHECK(rainy.smear.y > windy.smear.y);
    // A normal's horizontal part is below one, and the tilt is 0.08 metres per metre of it.
    water_style steepest = style;
    steepest.ripple_slope = 0.5f;
    steepest.fine_slope = 0.5f;
    steepest.rain_ring_slope = 1.0f;
    CHECK(water_reflection_reach(steepest, 40.0f, 1.0f).tilt < 0.08f);
}

TEST_CASE("a crop takes a picture's pixels to the same pixels of a smaller capture") {
    const int width = 160;
    const int height = 90;
    const reflection_crop crop = snap_reflection_crop(glm::vec4(0.31f, 0.22f, 0.58f, 0.47f), width, height);
    CHECK(crop.width < width);
    CHECK(crop.height < height);
    CHECK(crop.x >= 0);
    CHECK(crop.x + crop.width <= width);
    // The rectangle and a texel of filtering are inside.
    CHECK(static_cast<float>(crop.x) <= 0.31f * width - 1.5f);
    CHECK(static_cast<float>(crop.x + crop.width) >= 0.58f * width + 0.5f);
    CHECK(static_cast<float>(crop.y) <= 0.22f * height - 1.5f);
    CHECK(static_cast<float>(crop.y + crop.height) >= 0.47f * height + 0.5f);

    const glm::mat4 to_crop = reflection_crop_matrix(crop);
    const glm::mat4 to_texture = reflection_texture_matrix(crop);
    for (const glm::vec2 ndc : {glm::vec2(-0.3f, -0.2f), glm::vec2(0.1f, 0.0f), glm::vec2(0.05f, -0.4f)}) {
        const glm::vec2 pixel = (ndc * 0.5f + 0.5f) * glm::vec2(width, height);  // in the full picture
        const glm::vec4 in_crop = to_crop * glm::vec4(ndc, 0.0f, 1.0f);
        const glm::vec2 captured = (glm::vec2(in_crop) * 0.5f + 0.5f) * glm::vec2(crop.width, crop.height);
        CHECK(near(captured.x, pixel.x - static_cast<float>(crop.x), 1.0e-3f));
        CHECK(near(captured.y, pixel.y - static_cast<float>(crop.y), 1.0e-3f));
        // Looked up, it is the texture's same texel position.
        const glm::vec4 in_texture = to_texture * in_crop;
        const glm::vec2 uv = glm::vec2(in_texture) * 0.5f + 0.5f;
        CHECK(near(uv.x * width, pixel.x - static_cast<float>(crop.x), 1.0e-3f));
        CHECK(near(uv.y * height, pixel.y - static_cast<float>(crop.y), 1.0e-3f));
    }
    // The whole picture is the identity.
    const reflection_crop whole = snap_reflection_crop(glm::vec4(0, 0, 1, 1), width, height);
    CHECK(whole.x == 0);
    CHECK(whole.y == 0);
    CHECK(whole.width == width);
    CHECK(whole.height == height);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(near(reflection_crop_matrix(whole)[column][row], column == row ? 1.0f : 0.0f, 1.0e-4f));
            CHECK(near(reflection_texture_matrix(whole)[column][row], column == row ? 1.0f : 0.0f, 1.0e-4f));
        }
    }
    // The clamp sits half a texel inside the picture's edges, wherever the crop is.
    const glm::vec4 range = reflection_lookup_range(crop);
    CHECK(near(range.x * width, 0.5f - static_cast<float>(crop.x), 1.0e-4f));
    CHECK(near(range.z * width, width - 0.5f - static_cast<float>(crop.x), 1.0e-4f));
    const glm::vec4 whole_range = reflection_lookup_range(whole);
    CHECK(near(whole_range.x * width, 0.5f, 1.0e-4f));
    CHECK(near(whole_range.w * height, height - 0.5f, 1.0e-4f));
}

TEST_CASE("a tiny or edge rectangle still holds the texels bilinear filtering reads") {
    for (const glm::vec4 rectangle : {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                      glm::vec4(0.5f, 0.5f, 0.5f, 0.5f)}) {
        const reflection_crop crop = snap_reflection_crop(rectangle, 160, 90);
        CHECK(crop.width >= 2);
        CHECK(crop.height >= 2);
        CHECK(crop.x >= 0);
        CHECK(crop.y >= 0);
        CHECK(crop.x + crop.width <= 160);
        CHECK(crop.y + crop.height <= 90);
    }
}

TEST_CASE("the rectangle holds every lookup the shader can make, from near and grazing cameras") {
    const water_style style = shipped_style();
    random_unit random;
    int bounded = 0;
    int whole_picture = 0;
    for (const auto& [wind, rain] : {std::pair{0.0f, 0.0f}, std::pair{6.0f, 0.0f}, std::pair{25.0f, 1.0f}}) {
        const water_lookup_reach reach = water_reflection_reach(style, wind, rain);
        for (int trial = 0; trial < 120; ++trial) {
            const camera c = random_camera(random, 0.3f, 40.0f);
            const float level = random.between(-3.0f, 0.0f);
            const glm::vec2 low(random.between(-80, 40), random.between(-80, 40));
            const water_cluster cluster = cluster_at(low, low + glm::vec2(random.between(1, 32), random.between(1, 32)), level);
            const water_cluster_cover cover = water_cluster_coverage(cluster, level, c.view_proj, c.eye, reach);
            for (int sample = 0; sample < 200; ++sample) {
                const glm::vec3 point(random.between(cluster.min.x, cluster.max.x), level, random.between(cluster.min.z, cluster.max.z));
                const glm::vec4 clip = c.view_proj * glm::vec4(point, 1.0f);
                const bool on_screen = clip.w > 0.0f && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= -clip.w;
                if (!on_screen) { continue; }
                // A fragment of this cluster is shaded, so it must have been found.
                REQUIRE(cover.kind == water_cover::rectangle);
                const float distance = glm::distance(c.eye, point);
                const float angle = random.between(0.0f, 6.2831853f);
                const float tilt = reach.tilt * distance * random.next();
                const glm::vec3 tilted_point = point + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * tilt;
                const glm::vec4 tilted = c.view_proj * glm::vec4(tilted_point, 1.0f);
                // The shader's own lookup, floored at 0.01 of depth.
                const glm::vec2 uv = glm::vec2(tilted) / std::max(tilted.w, 0.01f) * 0.5f + 0.5f;
                const glm::vec2 tap = reach.smear * (random.between(-1.0f, 1.0f));
                const glm::vec2 looked = glm::clamp(uv + tap, glm::vec2(0.0f), glm::vec2(1.0f));
                CHECK(contains(cover.rectangle, looked));
            }
            if (cover.kind == water_cover::rectangle) {
                (cover.rectangle == glm::vec4(0, 0, 1, 1) ? whole_picture : bounded) += 1;
            }
        }
    }
    // Most views are bounded; the rest fell back to the whole picture.
    CHECK(bounded > whole_picture);
}

TEST_CASE("water behind the eye or off to the side has no rectangle; water ahead has a small one") {
    const water_style style = shipped_style();
    const water_lookup_reach reach = water_reflection_reach(style, 4.0f, 0.0f);
    const glm::vec3 eye(0, 2, 0);
    const glm::mat4 view_proj = glm::perspective(glm::radians(50.0f), 16.0f / 9.0f, 0.1f, 1000.0f) *
                                glm::lookAt(eye, eye + glm::vec3(0, -0.2f, 1), glm::vec3(0, 1, 0));
    CHECK(water_cluster_coverage(cluster_at({-10, -60}, {10, -20}, 0), 0, view_proj, eye, reach).kind == water_cover::outside);
    CHECK(water_cluster_coverage(cluster_at({200, 10}, {230, 40}, 0), 0, view_proj, eye, reach).kind == water_cover::outside);
    const water_cluster_cover ahead = water_cluster_coverage(cluster_at({-6, 25}, {6, 40}, 0), 0, view_proj, eye, reach);
    REQUIRE(ahead.kind == water_cover::rectangle);
    CHECK(ahead.rectangle.z - ahead.rectangle.x < 0.5f);
    CHECK(ahead.rectangle.w - ahead.rectangle.y < 0.3f);
    // The picture's centre column is inside it.
    CHECK(ahead.rectangle.x < 0.5f);
    CHECK(ahead.rectangle.z > 0.5f);
}

TEST_CASE("water that crosses the eye's plane captures the whole picture rather than guess") {
    const water_style style = shipped_style();
    const water_lookup_reach reach = water_reflection_reach(style, 8.0f, 0.0f);
    const glm::vec3 eye(0, 0.4f, 0);
    const glm::mat4 view_proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.05f, 1000.0f) *
                                glm::lookAt(eye, eye + glm::vec3(0, -1, 0.05f), glm::vec3(0, 1, 0));
    const water_cluster_cover under = water_cluster_coverage(cluster_at({-30, -30}, {30, 30}, 0), 0, view_proj, eye, reach);
    REQUIRE(under.kind == water_cover::rectangle);
    CHECK(under.rectangle == glm::vec4(0, 0, 1, 1));
}

TEST_CASE("candidates carry their rectangle; water seen from under needs none and none of it in view is dropped") {
    render_water water;
    water.revision = 1;
    for (const glm::vec3 p : {glm::vec3(-8, 0, 20), glm::vec3(8, 0, 20), glm::vec3(0, 0, 40)}) {
        render_water_vertex v;
        v.position = p;
        water.triangles.push_back(v);
    }
    index_render_water(water);
    const water_lookup_reach reach = water_reflection_reach(shipped_style(), 3.0f, 0.0f);
    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    const glm::vec3 eye(0, 1.7f, 0);
    const auto candidates = water_candidates(water, projection * glm::lookAt(eye, eye + glm::vec3(0, -0.15f, 1), glm::vec3(0, 1, 0)), eye, reach);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].coverage.z - candidates[0].coverage.x < 0.6f);
    CHECK(candidates[0].coverage.z > 0.5f);
    const glm::vec3 below(0, -1.0f, 0);
    const auto from_under = water_candidates(water, projection * glm::lookAt(below, below + glm::vec3(0, 0.15f, 1), glm::vec3(0, 1, 0)), below, reach);
    REQUIRE(from_under.size() == 1);
    CHECK(from_under[0].coverage == glm::vec4(0, 0, 1, 1));
}

TEST_CASE("a mirrored projection crops what it sees and keeps the clip plane and the sky's sign") {
    const float level = 3;
    const glm::vec3 eye(0, 5, 0);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0, 3, 20), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 1000.0f);
    const reflection_crop crop = snap_reflection_crop(glm::vec4(0.3f, 0.2f, 0.7f, 0.5f), 160, 90);
    const auto whole = reflect_water_view(view, proj, eye, level);
    const auto cropped = reflect_water_view(view, proj, eye, level, reflection_crop_matrix(crop));
    CHECK(cropped.sky_projection != proj);
    // The cropped frustum no longer sees a point the whole one sees at the picture's corner...
    const glm::vec4 corner_point(-8, 8, 20, 1);
    const glm::vec4 a = whole.projection * whole.view * corner_point;
    const glm::vec4 b = cropped.projection * cropped.view * corner_point;
    CHECK(std::abs(a.x) <= a.w);
    CHECK(std::abs(b.x) > b.w);
    // ... while above and below the surface keep their sides of the clip plane.
    const glm::vec4 above = cropped.projection * cropped.view * glm::vec4(0, 8, 20, 1);
    const glm::vec4 under = cropped.projection * cropped.view * glm::vec4(0, 2, 20, 1);
    CHECK(above.z > -above.w);
    CHECK(under.z < -under.w);
    // And a point projects to the same pixel of the crop as of the whole picture.
    const glm::vec4 p(2, 3, 24, 1);
    const glm::vec4 in_whole = whole.projection * whole.view * p;
    const glm::vec4 in_crop = cropped.projection * cropped.view * p;
    const glm::vec2 pixel_whole = (glm::vec2(in_whole) / in_whole.w * 0.5f + 0.5f) * glm::vec2(160, 90);
    const glm::vec2 pixel_crop = (glm::vec2(in_crop) / in_crop.w * 0.5f + 0.5f) * glm::vec2(crop.width, crop.height);
    CHECK(near(pixel_crop.x, pixel_whole.x - static_cast<float>(crop.x), 1.0e-3f));
    CHECK(near(pixel_crop.y, pixel_whole.y - static_cast<float>(crop.y), 1.0e-3f));
}
