#include "doctest.h"

#include "renderer/shadow_map_fit.h"

#include "test_support.h"

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <initializer_list>

namespace {
constexpr float extent = 160.0f;
constexpr int resolution = 1024;
constexpr float ahead = 0.3f;

const glm::vec3 low_sun = glm::normalize(glm::vec3(-0.8f, 0.25f, 0.4f));

// Where `world` lands on the map, in texels across, and its depth 0..1.
glm::vec3 on_map(const shadow_map_fit& fit, const glm::vec3& world) {
    const glm::vec4 clip = fit.view_proj * glm::vec4(world, 1.0f);
    return glm::vec3((clip.x * 0.5f + 0.5f) * resolution, (clip.y * 0.5f + 0.5f) * resolution, clip.z * 0.5f + 0.5f);
}

float fraction(const float value) {
    return value - std::floor(value);
}
}

TEST_CASE("the shadow map looks down the sun at the ground ahead of the camera") {
    const glm::vec3 eye(10.0f, 3.0f, 20.0f);
    const glm::vec3 forward(0.0f, -0.2f, -1.0f);
    const shadow_map_fit fit = fit_shadow_map(low_sun, eye, forward, extent, resolution, ahead);
    const glm::vec3 middle = on_map(fit, eye + glm::vec3(0.0f, 0.0f, -extent * ahead));
    CHECK(std::abs(middle.x - resolution * 0.5f) <= 1.0f);
    CHECK(std::abs(middle.y - resolution * 0.5f) <= 1.0f);
    CHECK(near(middle.z, 0.5f, 0.001f));

    // A tree's crown up the sun from a spot shades exactly that spot, and is
    // nearer the sun in depth.
    const glm::vec3 spot = eye + glm::vec3(5.0f, -3.0f, -30.0f);
    const glm::vec3 crown = on_map(fit, spot + low_sun * 12.0f);
    const glm::vec3 ground = on_map(fit, spot);
    CHECK(near(crown.x, ground.x, 0.01f));
    CHECK(near(crown.y, ground.y, 0.01f));
    CHECK(crown.z < ground.z);

    // The camera's own spot is on the map, well inside its edge.
    const glm::vec3 under_eye = on_map(fit, eye);
    CHECK(under_eye.x > 0.0f);
    CHECK(under_eye.x < resolution);
    CHECK(under_eye.y > 0.0f);
    CHECK(under_eye.y < resolution);
    CHECK(fit.depth_range > extent);
}

TEST_CASE("the shadow map's texels stay put on the ground as the camera moves") {
    const glm::vec3 fixed(37.3f, 1.2f, -54.9f);
    const glm::vec3 forward(0.3f, 0.0f, -1.0f);
    const shadow_map_fit first = fit_shadow_map(low_sun, glm::vec3(0.0f, 2.0f, 0.0f), forward, extent, resolution, ahead);
    const glm::vec3 reference = on_map(first, fixed);
    for (const float step : {0.013f, 0.051f, 0.37f, 1.9f, 7.77f}) {
        const glm::vec3 eye(step, 2.0f + step * 0.1f, -step * 2.0f);
        const glm::vec3 moved = on_map(fit_shadow_map(low_sun, eye, forward, extent, resolution, ahead), fixed);
        // Whole texels only: the same place within its texel.
        CHECK(std::abs(fraction(moved.x - reference.x + 0.5f) - 0.5f) < 0.01f);
        CHECK(std::abs(fraction(moved.y - reference.y + 0.5f) - 0.5f) < 0.01f);
    }
}

TEST_CASE("the shadow map's light stays well defined with the sun overhead") {
    for (const glm::vec3& sun : {glm::vec3(0.0f, 1.0f, 0.0f), glm::normalize(glm::vec3(0.0001f, 1.0f, 0.0f))}) {
        const glm::mat3 rotation = shadow_light_rotation(sun);
        CHECK(near(rotation * sun, glm::vec3(0.0f, 0.0f, 1.0f), 0.0001f));
        CHECK(near(glm::length(rotation * glm::vec3(1.0f, 0.0f, 0.0f)), 1.0f, 0.0001f));
        const glm::vec3 middle =
            on_map(fit_shadow_map(sun, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), extent, resolution, 0.0f), glm::vec3(0.0f));
        CHECK(std::isfinite(middle.x));
        CHECK(std::abs(middle.x - resolution * 0.5f) <= 1.0f);
    }
}
