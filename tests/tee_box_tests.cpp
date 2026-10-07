#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/tee_box.h"
#include "physics/terrain.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <cmath>
#include <cstddef>

#include <glm/geometric.hpp>

namespace {
// Points over the box's footprint, its edges included.
template <typename visit>
void for_footprint(const tee_box& box, const float inset, const visit& at) {
    const glm::vec3 across(-box.down_hole.z, 0.0f, box.down_hole.x);
    for (int i = 0; i <= 8; ++i) {
        for (int j = 0; j <= 8; ++j) {
            const float x = (box.half_width - inset) * (static_cast<float>(i) / 4.0f - 1.0f);
            const float y = (box.half_length - inset) * (static_cast<float>(j) / 4.0f - 1.0f);
            at(box.center + across * x + box.down_hole * y);
        }
    }
}

float ground_height(const play_area& area, const glm::vec3& position) {
    return sample_terrain_mesh(area.ground, position, area.ground_y).point.y;
}
}

TEST_CASE("every hole of a hub course gets a tee box on its tee, facing like its sign") {
    const game_state state = started_game(fixture_hub_course());
    REQUIRE(state.hub);
    REQUIRE(state.area.tee_boxes.size() == state.hub->markers.size());
    for (std::size_t i = 0; i < state.area.tee_boxes.size(); ++i) {
        const tee_box& box = state.area.tee_boxes[i];
        CHECK(horizontal_distance(box.center, state.hub->markers[i].tee_position) < 0.001f);
        CHECK(near(box.down_hole, state.area.signs[i].down_hole));
        CHECK(box.half_width * 2.0f == state.tuning.tee_box.width);
        CHECK(box.half_length * 2.0f == state.tuning.tee_box.length);
    }
}

TEST_CASE("a tee box on a slope is flat over its highest ground, its sides down to its lowest") {
    const play_area area =
        hole_area(straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 12.0f, 120.0f), 30.0f), shipped_content().tuning);
    REQUIRE(area.tee_boxes.size() == 1U);
    const tee_box& box = area.tee_boxes.front();

    float highest = -1000.0f;
    float lowest = 1000.0f;
    for_footprint(box, 0.0f, [&](const glm::vec3& point) {
        const float ground = ground_height(area, point);
        highest = std::max(highest, ground);
        lowest = std::min(lowest, ground);
        // Never into the ground, anywhere on it.
        CHECK(box.center.y > ground);
    });
    CHECK(highest - lowest > 0.3f);  // the slope shows across the box
    CHECK(box.center.y - highest < shipped_content().tuning.tee_box.padding + 0.05f);
    CHECK(box.bottom <= lowest + 0.001f);

    // Its top is the surface on it: flat and level. Off it, the ground.
    for_footprint(box, 0.01f, [&](const glm::vec3& point) {
        const terrain_sample sample = sample_area(area, point);
        CHECK(std::abs(sample.point.y - box.center.y) < 1e-5f);
        CHECK(near(sample.normal, world_up));
    });
    const glm::vec3 beyond = box.center + box.down_hole * (box.half_length + 2.0f);
    CHECK(std::abs(terrain_height(area, beyond) - ground_height(area, beyond)) < 1e-5f);
}

TEST_CASE("a teed-up ball rests on the tee box") {
    const game_state state = started_hole();
    REQUIRE(state.area.tee_boxes.size() == state.course_holes.size());
    const tee_box& box = state.area.tee_boxes[state.hole->index];
    CHECK(on_tee_box(box, state.ball.position));
    CHECK(std::abs(state.ball.position.y - (box.center.y + state.ball.radius)) < 1e-4f);
    CHECK(on_tee_box(box, state.player.position));
    CHECK(std::abs(state.player.position.y - box.center.y) < 1e-4f);
}
