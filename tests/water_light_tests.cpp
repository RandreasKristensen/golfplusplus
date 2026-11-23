#include "doctest.h"

#include "game/content_files.h"
#include "game/lighting_loader.h"
#include "renderer/water_light.h"
#include "renderer/water_reflection.h"
#include <glm/gtc/matrix_transform.hpp>
#include "test_support.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

#include <glm/geometric.hpp>

namespace {
water_style shipped_water() {
    const std::optional<lighting_palette> palette = load_lighting(fixture_root());
    return palette ? palette->water : water_style{};
}
}

TEST_CASE("water takes red out fastest and nothing out of no depth") {
    const water_style water = shipped_water();
    REQUIRE(water.max_path_meters > 0.0f);
    const glm::vec3 none = water_transmittance(water, 0.0f);
    CHECK(near(none.x, 1.0f));
    CHECK(near(none.y, 1.0f));
    CHECK(near(none.z, 1.0f));
    const glm::vec3 deep = water_transmittance(water, 3.0f);
    CHECK(deep.x < deep.y);
    CHECK(deep.x < deep.z);
    CHECK(deep.y > 0.0f);
    CHECK(deep.x < none.x);
    // Deeper never lets more through.
    const glm::vec3 deeper = water_transmittance(water, 4.0f);
    CHECK(deeper.x < deep.x);
    CHECK(deeper.y < deep.y);
    CHECK(deeper.z < deep.z);
}

TEST_CASE("a pond shows the bank in the first half metre and has turned a dark, faintly blue grey by three") {
    const water_style water = shipped_water();
    const glm::vec3 white(1.0f);
    const glm::vec3 light(0.8f);
    CHECK(near(water_seen_through(water, white, light, 0.0f), white));
    const glm::vec3 bank = water_seen_through(water, white, light, 0.5f);
    CHECK(bank.x > 0.4f);
    CHECK(bank.y > bank.x);
    const glm::vec3 middle = water_seen_through(water, white, light, water_path_meters(water, 3.0f));
    // Blue leads green only slightly: a hue, not a blue pond, and never green.
    CHECK(middle.z > middle.y);
    CHECK(middle.y > middle.x);
    CHECK(middle.z < 1.6f * middle.y);
    // Most of the bank's light is gone, and what is left is the water's colour.
    CHECK(glm::length(middle) < 0.5f * glm::length(bank));
    CHECK(glm::length(middle - water.scatter_color * light) < 0.5f * glm::length(white - water.scatter_color * light));
}

TEST_CASE("a long path sees only the water's own lit colour, and no light gives none") {
    const water_style water = shipped_water();
    const glm::vec3 light(0.8f, 0.7f, 0.6f);
    const glm::vec3 far = water_seen_through(water, glm::vec3(1.0f), light, 1000.0f);
    CHECK(near(far, water.scatter_color * light, 0.001f));
    CHECK(near(water_path_meters(water, 1000.0f), water.max_path_meters));
    CHECK(near(water_path_meters(water, -1.0f), 0.0f));
    CHECK(near(glm::length(water_seen_through(water, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f)), 0.0f));
    // Red is lost fastest: a red bed fades further than a green one.
    const glm::vec3 red_bed = water_seen_through(water, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), 1.0f);
    const glm::vec3 green_bed = water_seen_through(water, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f), 1.0f);
    CHECK(red_bed.x < green_bed.y);
}

TEST_CASE("the surface reflects little looking down and nearly all at a glancing angle") {
    CHECK(near(water_fresnel(1.0f), water_f0));
    CHECK(near(water_fresnel(0.0f), 1.0f));
    CHECK(water_fresnel(0.2f) > water_fresnel(0.8f));
}

TEST_CASE("distant rough water hides a smooth share of its reflection in the next wave, never all or none") {
    const water_style water = shipped_water();
    const float slope = water.ripple_slope + water.fine_slope;
    // A 1.7 m eye: the reflected ray rises as steeply as the view falls.
    float previous = 0.0f;
    for (const float meters : {5.0f, 20.0f, 60.0f, 150.0f, 400.0f}) {
        const float up = 1.7f / std::sqrt(meters * meters + 1.7f * 1.7f);
        const float share = water_masked_share(up, slope);
        CHECK(share >= previous);
        previous = share;
    }
    // Far off, rough water is partly masked: a blend, not dark specks or a mirror.
    const float far = water_masked_share(1.7f / 400.0f, slope);
    CHECK(far > 0.2f);
    CHECK(far < 0.8f);
    // A calm mirror hides nothing seen from above, and nothing is hidden looking down.
    CHECK(near(water_masked_share(0.05f, 0.0f), 0.0f));
    CHECK(near(water_masked_share(1.0f, slope), 0.0f));
    // Every share is a blend between nothing and all of it, smooth in the height.
    for (int i = -100; i <= 100; ++i) {
        const float up = static_cast<float>(i) / 200.0f;
        const float share = water_masked_share(up, slope);
        CHECK(share >= 0.0f);
        CHECK(share <= 1.0f);
        CHECK(std::abs(water_masked_share(up + 0.005f, slope) - share) < 0.05f);
    }
}

TEST_CASE("the shader masks the next wave as the CPU does") {
    const std::optional<std::string> source =
        read_text_file(std::filesystem::path(asset_root()) / "picture" / "shaders" / "water.frag");
    REQUIRE(source.has_value());
    for (const char* text : {"float reach = 2.0 * slope + 0.02;", "return 1.0 - smoothstep(-reach, reach, up);",
                             "float masked = water_masked_share(bounce.y, smear);"}) {
        CHECK(source->find(text) != std::string::npos);
    }
}

TEST_CASE("distant water retains the Fresnel response of unresolved waves") {
    // A 1.7 m eye looking at water 100 m away. A flat normal would reflect
    // almost the entire pale horizon, even with wind-driven waves present.
    const float cosine = 1.7f / std::sqrt(100.0f * 100.0f + 1.7f * 1.7f);
    const water_style water = shipped_water();
    const float slope = water.ripple_slope + water.fine_slope;
    REQUIRE(slope > 0.0f);
    CHECK(water_fresnel(cosine) > 0.9f);
    CHECK(water_filtered_fresnel(cosine, slope) < 0.6f);
    CHECK(water_filtered_fresnel(cosine, slope) > water_f0);
    // Filtering a perfectly calm surface changes nothing, including grazing.
    for (int i = 0; i <= 100; ++i) {
        const float c = static_cast<float>(i) / 100.0f;
        CHECK(near(water_filtered_fresnel(c, 0.0f), water_fresnel(c)));
        const float filtered = water_filtered_fresnel(c, slope);
        CHECK(filtered >= water_f0);
        CHECK(filtered <= 1.0f);
    }
    CHECK(water_filtered_fresnel(0.0f, slope) < 0.6f);
    CHECK(water_filtered_fresnel(1.0f, slope) < 0.021f);
}

namespace {
float degrees_to_cosine(const float degrees) {
    return std::cos(degrees * 3.14159265f / 180.0f);
}
}

TEST_CASE("from under the surface the sky shows straight up, its edge sits at the critical angle and beyond it is the water's own murk") {
    CHECK(water_see_above_share(1.0f) > 0.95f);
    CHECK(near(water_see_above_share(degrees_to_cosine(60.0f)), 0.0f));
    CHECK(near(water_see_above_share(degrees_to_cosine(89.0f)), 0.0f));
    // The window closes at asin(1 / 1.33) from straight up.
    float edge = -1.0f;
    for (float degrees = 0.0f; degrees <= 90.0f; degrees += 0.05f) {
        if (water_snell_window(degrees_to_cosine(degrees)) < 0.5f) {
            edge = degrees;
            break;
        }
    }
    CHECK(std::abs(edge - 48.6f) < 1.0f);
    // Looking further from vertical never shows more of what is above.
    float previous = 2.0f;
    for (float degrees = 0.0f; degrees <= 90.0f; degrees += 1.0f) {
        const float share = water_see_above_share(degrees_to_cosine(degrees));
        CHECK(share <= previous + 1.0e-6f);
        previous = share;
    }
    // The surface lets nearly everything out straight up and none at the edge.
    CHECK(near(water_fresnel_from_below(1.0f), water_f0));
    CHECK(near(water_fresnel_from_below(water_critical_cosine()), 1.0f, 0.01f));
}

TEST_CASE("the shaders carry the window's edge the CPU works out") {
    char expected[64] = {};
    std::snprintf(expected, sizeof expected, "window_edge_cosine = %.4f;", water_critical_cosine());
    const std::optional<std::string> source =
        read_text_file(std::filesystem::path(asset_root()) / "picture" / "shaders" / "water.frag");
    REQUIRE(source.has_value());
    CHECK(source->find(expected) != std::string::npos);
}

TEST_CASE("deeper water is darker, red goes first and blue last") {
    const water_style water = shipped_water();
    const glm::vec3 shallow = water_light_at_depth(water, 0.5f);
    const glm::vec3 deep = water_light_at_depth(water, 3.0f);
    CHECK(near(glm::length(water_light_at_depth(water, 0.0f) - glm::vec3(1.0f)), 0.0f));
    CHECK(near(glm::length(water_light_at_depth(water, -2.0f) - glm::vec3(1.0f)), 0.0f));
    for (int channel = 0; channel < 3; ++channel) {
        CHECK(deep[channel] < shallow[channel]);
    }
    // Red goes first and blue last: the water is faintly blue, never green.
    CHECK(deep.x < deep.y);
    CHECK(deep.y < deep.z);
}

TEST_CASE("still water is a mirror, a light breeze breaks it and a fresh one leaves nothing whole") {
    const water_style water = shipped_water();
    REQUIRE(water.rough_wind_meters_per_second > water.calm_wind_meters_per_second);
    CHECK(near(water_wind_roughness(water, 0.0f), 0.0f));
    CHECK(near(water_wind_roughness(water, water.calm_wind_meters_per_second), 0.0f));
    CHECK(water_wind_roughness(water, 2.0f) > 0.2f);
    CHECK(near(water_wind_roughness(water, water.rough_wind_meters_per_second + 1.0f), 1.0f));
    // Open water in a fresh wind is fully rough; still air leaves the calm share.
    CHECK(near(water_roughness(water, 8.0f, water.full_fetch_meters * 2.0f, 1.0f), 1.0f));
    CHECK(near(water_roughness(water, 0.0f, water.full_fetch_meters, 1.0f), water.calm_ripple_share));
}

TEST_CASE("the upwind shore stays glassy and the waves grow across the pond, less between gusts") {
    const water_style water = shipped_water();
    const float shore = water_roughness(water, 6.0f, 0.0f, 1.0f);
    const float middle = water_roughness(water, 6.0f, 0.3f * water.full_fetch_meters, 1.0f);
    const float far_side = water_roughness(water, 6.0f, water.full_fetch_meters, 1.0f);
    CHECK(near(shore, water.calm_ripple_share));
    CHECK(shore < middle);
    CHECK(middle < far_side);
    CHECK(water_roughness(water, 6.0f, water.full_fetch_meters, 0.0f) < far_side);
}


TEST_CASE("water mirrors the real world and clips below the surface, without moving projected surface points") {
    const float level = 3;
    const glm::vec3 eye(0,5,0);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0,3,20), glm::vec3(0,1,0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f),1.5f,0.1f,1000.0f);
    const auto mirror = reflect_water_view(view,proj,eye,level);
    CHECK(near(mirror.eye,glm::vec3(0,1,0)));
    for (const glm::vec3 p : {glm::vec3(-4,3,20),glm::vec3(4,3,40)}) {
        const glm::vec4 a = proj * view * glm::vec4(p,1);
        const glm::vec4 b = mirror.projection * mirror.view * glm::vec4(p,1);
        CHECK(near(a.x/a.w,b.x/b.w)); CHECK(near(a.y/a.w,b.y/b.w));
    }
    const glm::vec4 above = mirror.projection * mirror.view * glm::vec4(0,8,20,1);
    const glm::vec4 under = mirror.projection * mirror.view * glm::vec4(0,2,20,1);
    CHECK(above.z > -above.w); CHECK(under.z < -under.w);
    const glm::vec4 actual = proj * view * glm::vec4(0,8,20,1);
    // Its reflection is below the water in the picture.
    CHECK(above.y/above.w < actual.y/actual.w);
}

TEST_CASE("visible water planes are deduplicated, sorted, and culled independently") {
    render_water water;
    for (const float level : {2.0f,1.0f,2.0f}) {
        for (const glm::vec3 p : {glm::vec3(-4,level,20),glm::vec3(4,level,20),glm::vec3(0,level,30)}) {
            render_water_vertex v; v.position=p; water.triangles.push_back(v);
        }
    }
    index_render_water(water);
    const glm::vec3 eye(0,4,0);
    const glm::mat4 vp = glm::perspective(glm::radians(60.0f),1.5f,0.1f,100.0f) *
        glm::lookAt(eye,glm::vec3(0,2,20),glm::vec3(0,1,0));
    const auto levels=water_candidates(water,vp,eye,water_lookup_reach{});
    REQUIRE(levels.size()==2); CHECK(levels[0].level==1); CHECK(levels[1].level==2);
    for (auto& vertex:water.triangles) { vertex.position.z = -vertex.position.z; }
    index_render_water(water);
    CHECK(water_candidates(water,vp,eye,water_lookup_reach{}).empty());
}

TEST_CASE("water levels group only exactly equal heights and keep exact bounds per level and cell") {
    render_water water;
    const float near_level = std::nextafter(1.0f, 2.0f);
    const auto add = [&](const float level, const glm::vec2 centre) {
        for (const glm::vec2 corner : {glm::vec2(-2, -2), glm::vec2(2, -2), glm::vec2(0, 3)}) {
            render_water_vertex v; v.position = {centre.x + corner.x, level, centre.y + corner.y}; water.triangles.push_back(v);
        }
    };
    add(1.0f, {500, 10}); add(near_level, {0, 10}); add(1.0f, {-500, 10}); add(1.0f, {500, 12});
    index_render_water(water);
    REQUIRE(water.levels.size() == 2);
    CHECK(water.levels[0].level == 1.0f);
    CHECK(water.levels[1].level == near_level);
    CHECK(water.levels[0].vertex_count == 9);
    CHECK(water.levels[0].cluster_count == 2);
    CHECK(water.levels[1].cluster_count == 1);
    CHECK(water.levels[0].min.x == -502.0f); CHECK(water.levels[0].max.x == 502.0f);
    CHECK(water.levels[1].first_vertex == 9);
    for (const water_level_range& range : water.levels) {
        int vertices = 0;
        for (int c = 0; c < range.cluster_count; ++c) {
            const water_cluster& cluster = water.clusters[static_cast<std::size_t>(range.first_cluster + c)];
            CHECK(cluster.first_vertex == range.first_vertex + vertices);
            vertices += cluster.vertex_count;
            for (int v = 0; v < cluster.vertex_count; ++v) {
                const glm::vec3 p = water.triangles[static_cast<std::size_t>(cluster.first_vertex + v)].position;
                CHECK(p.y == range.level);
                CHECK(glm::all(glm::greaterThanEqual(p, cluster.min)));
                CHECK(glm::all(glm::lessThanEqual(p, cluster.max)));
            }
        }
        CHECK(vertices == range.vertex_count);
    }
    // Looking at the middle: level 1's bounds span the view but none of its clusters do.
    const glm::mat4 middle = glm::perspective(glm::radians(20.0f), 1.0f, 0.1f, 2000.0f) *
        glm::lookAt(glm::vec3(0, 5, -100), glm::vec3(0, 1, 10), glm::vec3(0, 1, 0));
    const auto candidates = water_candidates(water, middle, glm::vec3(0, 5, -100), water_lookup_reach{});
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].level == near_level);
    CHECK(candidates[0].query == 0);
}
