#include "doctest.h"

#include "renderer/caster_motion.h"
#include "renderer/shadow_map_fit.h"

#include "test_support.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <initializer_list>
#include <vector>

namespace {
constexpr float extent = 160.0f;
constexpr int resolution = 1024;

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

TEST_CASE("the shadow map looks down the sun centred on the camera position") {
    const glm::vec3 eye(10.0f, 3.0f, 20.0f);
    const shadow_map_fit fit = fit_shadow_map(low_sun, eye, extent, resolution);
    const glm::vec3 middle = on_map(fit, eye);
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
    const shadow_map_fit first = fit_shadow_map(low_sun, glm::vec3(0.0f, 2.0f, 0.0f), extent, resolution);
    const glm::vec3 reference = on_map(first, fixed);
    for (const float step : {0.013f, 0.051f, 0.37f, 1.9f, 7.77f}) {
        const glm::vec3 eye(step, 2.0f + step * 0.1f, -step * 2.0f);
        const glm::vec3 moved = on_map(fit_shadow_map(low_sun, eye, extent, resolution), fixed);
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
            on_map(fit_shadow_map(sun, glm::vec3(0.0f), extent, resolution), glm::vec3(0.0f));
        CHECK(std::isfinite(middle.x));
        CHECK(std::abs(middle.x - resolution * 0.5f) <= 1.0f);
    }
}

TEST_CASE("panning the camera changes screen position without moving a world's shadow lookup") {
    const glm::vec3 eye(10.0f, 3.0f, 20.0f);
    const glm::vec3 spot = eye + glm::vec3(0.0f, 0.0f, -55.0f);
    const glm::vec3 reference = on_map(fit_shadow_map(low_sun, eye, extent, resolution), spot);
    const glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1.0f, 0.1f, 500.0f);
    float previous_x = 2.0f;
    for (const float yaw : {-20.0f, 0.0f, 20.0f}) {
        const float angle = glm::radians(yaw);
        const glm::vec3 forward(std::sin(angle), 0.0f, -std::cos(angle));
        const glm::mat4 view_proj = projection * glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec4 clip = view_proj * glm::vec4(spot, 1.0f);
        const glm::vec3 screen = glm::vec3(clip) / clip.w;
        CHECK(std::abs(screen.x) < 1.0f);
        CHECK(screen.x < previous_x);
        previous_x = screen.x;
        // Follow this same visible patch through each view, including its depth.
        const glm::vec4 world = glm::inverse(view_proj) * glm::vec4(screen, 1.0f);
        const shadow_map_fit fit = fit_shadow_map(low_sun, eye, extent, resolution);
        CHECK(near(on_map(fit, glm::vec3(world) / world.w), reference, 0.1f));
    }
}

TEST_CASE("a held shadow map is the same map while the camera stays on its texel and within an extent of its depth") {
    const glm::vec3 eye(10.0f, 3.0f, 20.0f);
    const shadow_map_fit held = fit_shadow_map(low_sun, eye, extent, resolution);
    const float texel = extent / resolution;
    // A step of a hundredth of a texel across the sun keeps the corner; a texel or more moves it.
    const glm::mat3 light = shadow_light_rotation(low_sun);
    const glm::vec3 across = glm::transpose(light) * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 along = glm::transpose(light) * glm::vec3(0.0f, 0.0f, 1.0f);
    CHECK(fit_shadow_map(low_sun, eye + across * texel * 0.001f, extent, resolution).corner == held.corner);
    CHECK(fit_shadow_map(low_sun, eye + across * texel * 2.0f, extent, resolution).corner != held.corner);
    // Moving along the sun only slides the depth.
    const shadow_map_fit slid = fit_shadow_map(low_sun, eye + along * 30.0f, extent, resolution);
    CHECK(slid.corner == held.corner);
    CHECK(near(slid.depth_middle - held.depth_middle, 30.0f, 0.01f));
}

TEST_CASE("held casters count as unmoved until one has strayed past the tolerance from where it was drawn") {
    const std::vector<glm::vec3> drawn{{0.0f, 0.0f, 0.0f}, {5.0f, 1.0f, 2.0f}, {-3.0f, 2.0f, 8.0f}};
    caster_motion held;
    const auto list = [](const std::vector<glm::vec3>& positions) {
        return [&positions](const auto& visit) {
            for (const glm::vec3& position : positions) { visit(position); }
        };
    };
    CHECK(!held.within(list(drawn), 0.1f));
    held.hold(list(drawn));
    CHECK(held.within(list(drawn), 0.0f));
    std::vector<glm::vec3> flutter = drawn;
    flutter[1].x += 0.09f;
    CHECK(held.within(list(flutter), 0.1f));
    // Slow drift is measured against the drawn place, not the last frame's.
    flutter[1].x += 0.09f;
    CHECK(!held.within(list(flutter), 0.1f));
    // A figure gone or added is a different list.
    CHECK(!held.within(list({drawn.begin(), drawn.begin() + 2}), 0.1f));
    std::vector<glm::vec3> more = drawn;
    more.push_back({1.0f, 1.0f, 1.0f});
    CHECK(!held.within(list(more), 0.1f));
    held.forget();
    CHECK(!held.within(list(drawn), 0.1f));
}
