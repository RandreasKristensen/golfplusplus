#include "doctest.h"

#include "core/lightning_frame.h"
#include "game/content_files.h"
#include "renderer/world_bolt.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

namespace {
const lightning_style& style() {
    static const lighting_palette palette = *load_lighting(fixture_root());
    return palette.lightning;
}

const glm::vec3 camera(0.0f, 1.7f, 0.0f);

glm::mat4 view_projection() {
    return glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.05f, 5000.0f) *
        glm::lookAt(camera, camera + glm::vec3(0.0f, -0.05f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::vec2 on_screen(const glm::vec3& point) {
    const glm::vec4 clip = view_projection() * glm::vec4(point, 1.0f);
    CHECK(clip.w > 0.0f);
    return glm::vec2(clip.x, clip.y) / clip.w;
}

render_lightning real_strike(const glm::vec3& feet, const float age = 0.0f) {
    lightning_presentation state = strike_at(lightning_presentation{}, feet, camera, 1.0f, style());
    state.strike->age = age;
    return make_render_lightning(state, style(), camera);
}

const world_bolt_vertex& lowest(const std::vector<world_bolt_vertex>& bolt) {
    return *std::min_element(bolt.begin(), bolt.end(), [](const world_bolt_vertex& a, const world_bolt_vertex& b) {
        return a.position.y < b.position.y;
    });
}
}

TEST_CASE("a real strike's world bolt reaches the golfer's feet from high above, its lowest point on them on screen") {
    for (const glm::vec3& feet : {glm::vec3(3.0f, 0.0f, 20.0f), glm::vec3(-6.0f, 0.5f, 35.0f), glm::vec3(0.5f, -1.0f, 8.0f)}) {
        const render_lightning lightning = real_strike(feet);
        const std::vector<world_bolt_vertex> bolt = make_world_bolt(lightning, camera);
        REQUIRE(!bolt.empty());
        CHECK(bolt.size() % 3U == 0U);
        const world_bolt_vertex& bottom = lowest(bolt);
        CHECK(std::abs(bottom.position.y - feet.y) < 0.5f);
        CHECK(std::abs(bottom.position.x - feet.x) < 1.0f);
        CHECK(std::abs(bottom.position.z - feet.z) < 1.0f);
        // Seen from the camera, the bottom is where the golfer stands and the bolt rises above them.
        const glm::vec2 body = on_screen(feet);
        const glm::vec2 seen = on_screen(bottom.position);
        CHECK(std::abs(seen.x - body.x) < 0.05f);
        CHECK(std::abs(seen.y - body.y) < 0.05f);
        const world_bolt_vertex& highest = *std::max_element(bolt.begin(), bolt.end(), [](const world_bolt_vertex& a, const world_bolt_vertex& b) {
            return a.position.y < b.position.y;
        });
        CHECK(highest.position.y > feet.y + 0.9f * style().world_bolt_height_meters);
        // A little way up it rises over them on screen (its far top is over the camera's head).
        const world_bolt_vertex& above = *std::min_element(bolt.begin(), bolt.end(), [&](const world_bolt_vertex& x, const world_bolt_vertex& y) {
            return std::abs(x.position.y - feet.y - 15.0f) < std::abs(y.position.y - feet.y - 15.0f);
        });
        CHECK(on_screen(above.position).y > body.y + 0.05f);
        // Nothing of it lies below the golfer's feet but its own width.
        CHECK(bottom.position.y > feet.y - style().world_bolt_width_meters * 3.0f);
    }
}

TEST_CASE("a bolt on a golfer to the camera's left lands on the left of the picture, to its right on the right") {
    const auto side = [&](const float x) {
        return on_screen(lowest(make_world_bolt(real_strike(glm::vec3(x, 0.0f, 20.0f)), camera)).position).x;
    };
    // Looking along +z, the camera's left is +x.
    CHECK(side(4.0f) < 0.0f);
    CHECK(side(-4.0f) > 0.0f);
}

TEST_CASE("the same strike is the same bolt, another is another") {
    const glm::vec3 feet(3.0f, 0.0f, 20.0f);
    const std::vector<world_bolt_vertex> first = make_world_bolt(real_strike(feet), camera);
    const std::vector<world_bolt_vertex> again = make_world_bolt(real_strike(feet), camera);
    REQUIRE(first.size() == again.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].position == again[i].position);
    }
    render_lightning other = real_strike(feet);
    other.seed += 7.0f;
    const std::vector<world_bolt_vertex> changed = make_world_bolt(other, camera);
    bool differs = changed.size() != first.size();
    for (std::size_t i = 0; !differs && i < first.size(); ++i) {
        differs = changed[i].position != first[i].position;
    }
    CHECK(differs);
}

TEST_CASE("the bolt is jagged, its ribbons face the camera and are never narrower than the style says") {
    const glm::vec3 feet(3.0f, 0.0f, 20.0f);
    const std::vector<world_bolt_vertex> bolt = make_world_bolt(real_strike(feet), camera);
    REQUIRE(bolt.size() >= 6U);
    // The first quad's two sides are a ribbon wide, across the view.
    const glm::vec3 side = bolt[1].position - bolt[0].position;
    CHECK(glm::length(side) >= 3.0f * style().world_bolt_width_meters - 1e-4f);
    CHECK(std::abs(glm::dot(glm::normalize(side), glm::normalize(bolt[0].position - camera))) < 0.2f);
    // It wanders: the channel's points are not on one line.
    float widest = 0.0f;
    for (const world_bolt_vertex& vertex : bolt) {
        widest = std::max(widest, std::abs(vertex.position.x - feet.x));
    }
    CHECK(widest > 3.0f * style().world_bolt_width_meters);
}

TEST_CASE("nothing is drawn once the flash has ended, and a random strike has no bolt in the world") {
    const glm::vec3 feet(3.0f, 0.0f, 20.0f);
    CHECK(!make_world_bolt(real_strike(feet), camera).empty());
    render_lightning ended = real_strike(feet);
    ended.flash = lightning_flash(style().flash_seconds, ended.seed, style());
    CHECK(make_world_bolt(ended, camera).empty());
    CHECK(make_world_bolt(real_strike(feet, style().flash_seconds), camera).empty());

    lightning_presentation random;
    lightning_strike strike;
    strike.seed = 3.0f;
    random.strike = strike;
    const render_lightning sky = make_render_lightning(random, style(), camera);
    CHECK(sky.flash > 0.0f);
    CHECK(make_world_bolt(sky, camera).empty());
    CHECK(make_world_bolt(render_lightning{}, camera).empty());
}
