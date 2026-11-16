#include "doctest.h"

#include "game/content_files.h"
#include "game/lighting_loader.h"
#include "renderer/water_light.h"
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

TEST_CASE("a pond shows the bank in the first half metre and has turned dark green by three") {
    const water_style water = shipped_water();
    const glm::vec3 white(1.0f);
    const glm::vec3 light(0.8f);
    CHECK(near(water_seen_through(water, white, light, 0.0f), white));
    const glm::vec3 bank = water_seen_through(water, white, light, 0.5f);
    CHECK(bank.x > 0.4f);
    CHECK(bank.y > bank.x);
    const glm::vec3 middle = water_seen_through(water, white, light, water_path_meters(water, 3.0f));
    CHECK(middle.y > middle.x);
    CHECK(middle.y > middle.z);
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

TEST_CASE("the reflection march is clipped to the part of its segment on screen") {
    CHECK(shipped_water().reflection_reach_meters > 0.0f);
    // Wholly inside: all of it.
    water_screen_span span = water_reflection_screen_span({-0.5f, 0.0f}, {0.5f, 0.5f});
    CHECK(near(span.low, 0.0f));
    CHECK(near(span.high, 1.0f));
    // Leaving through the right edge: it stops at x = 1, three quarters along.
    span = water_reflection_screen_span({0.0f, 0.0f}, {4.0f / 3.0f, 0.0f});
    CHECK(near(span.low, 0.0f));
    CHECK(near(span.high, 0.75f));
    // Coming in from the left: it starts at x = -1.
    span = water_reflection_screen_span({-3.0f, 0.0f}, {1.0f, 0.0f});
    CHECK(near(span.low, 0.5f));
    CHECK(near(span.high, 1.0f));
    // Wholly off screen: nothing to march.
    span = water_reflection_screen_span({2.0f, 2.0f}, {3.0f, 2.5f});
    CHECK(span.low >= span.high);
    // Straight up from the middle: out the top.
    span = water_reflection_screen_span({0.0f, 0.0f}, {0.0f, 3.0f});
    CHECK(near(span.high, 1.0f / 3.0f));
}

TEST_CASE("a reflection is trusted in the picture's middle and not at its edge") {
    CHECK(near(water_reflection_edge_confidence(0.5f, 0.5f, 0.1f), 1.0f));
    CHECK(near(water_reflection_edge_confidence(0.0f, 0.5f, 0.1f), 0.0f));
    CHECK(near(water_reflection_edge_confidence(0.5f, 1.0f, 0.1f), 0.0f));
    float previous = 0.0f;
    for (int i = 0; i <= 10; ++i) {
        const float c = water_reflection_edge_confidence(static_cast<float>(i) * 0.01f, 0.5f, 0.1f);
        CHECK(c >= previous);
        previous = c;
    }
    CHECK(water_reflection_edge_confidence(0.05f, 0.5f, 0.1f) < water_reflection_edge_confidence(0.08f, 0.5f, 0.1f));
    CHECK(water_reflection_edge_confidence(0.95f, 0.5f, 0.1f) < water_reflection_edge_confidence(0.92f, 0.5f, 0.1f));
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

TEST_CASE("deeper water is darker, and red goes first") {
    const water_style water = shipped_water();
    const glm::vec3 shallow = water_light_at_depth(water, 0.5f);
    const glm::vec3 deep = water_light_at_depth(water, 3.0f);
    CHECK(near(glm::length(water_light_at_depth(water, 0.0f) - glm::vec3(1.0f)), 0.0f));
    CHECK(near(glm::length(water_light_at_depth(water, -2.0f) - glm::vec3(1.0f)), 0.0f));
    for (int channel = 0; channel < 3; ++channel) {
        CHECK(deep[channel] < shallow[channel]);
    }
    CHECK(deep.x < deep.z);
    CHECK(deep.z < deep.y);
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

TEST_CASE("the reflection's steps reach its reach, short near and long far") {
    const water_style water = shipped_water();
    const float growth = water_reflection_step_growth(water.reflection_first_step_meters, water.reflection_reach_meters,
                                                      water_reflection_steps);
    CHECK(growth > 1.0f);
    float walked = 0.0f;
    float step = water.reflection_first_step_meters;
    for (int i = 0; i < water_reflection_steps; ++i) {
        walked += step;
        step *= growth;
    }
    CHECK(std::abs(walked - water.reflection_reach_meters) < 0.01f * water.reflection_reach_meters);
    // Even steps that reach anyway need no growth.
    CHECK(near(water_reflection_step_growth(10.0f, 100.0f, 32), 1.0f));
}
