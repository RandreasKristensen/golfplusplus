#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/tee_box.h"
#include "physics/terrain.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
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

TEST_CASE("every hole of a course gets a tee box on its tee, facing like its sign") {
    const game_state state = started_game(fixture_hub_course());
    REQUIRE(course_started(state));
    REQUIRE(state.area.tee_boxes.size() == state.holes.size());
    for (std::size_t i = 0; i < state.area.tee_boxes.size(); ++i) {
        const tee_box& box = state.area.tee_boxes[i];
        CHECK(horizontal_distance(box.center, state.holes[i].tee_position) < 0.001f);
        CHECK(near(box.down_hole, state.area.signs[i].down_hole));
        CHECK(box.half_width * 2.0f == state.tuning.tee_box.width);
        CHECK(box.half_length * 2.0f == state.tuning.tee_box.length);
    }
}

TEST_CASE("a tee box on a slope is levelled into the ground at the land's mean, flush, its banks easing back") {
    const game_tuning& tuning = shipped_content().tuning;
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 12.0f, 120.0f), 30.0f);
    hole.spline.bank = {0.1f, 0.1f};
    const play_area area = hole_area(hole, tuning);
    // The same ground without the tee box: the land it is levelled from.
    hole_data bare = hole;
    bare.tee_box = false;
    const play_area land = hole_area(bare, tuning);
    REQUIRE(area.tee_boxes.size() == 1U);
    REQUIRE(land.tee_boxes.empty());
    const tee_box& box = area.tee_boxes.front();

    float highest = -1000.0f;
    float lowest = 1000.0f;
    float sum = 0.0f;
    float count = 0.0f;
    for_footprint(box, 0.0f, [&](const glm::vec3& point) {
        const float height = ground_height(land, point);
        highest = std::max(highest, height);
        lowest = std::min(lowest, height);
        sum += height;
        count += 1.0f;
    });
    CHECK(highest - lowest > 0.5f);  // the land slopes across the box
    CHECK(near(box.center.y, sum / count, 0.05f));

    // The ground under every point of it, its corners included, is its top:
    // flat and level, and so is the surface there.
    for_footprint(box, 0.0f, [&](const glm::vec3& point) {
        CHECK(near(ground_height(area, point), box.center.y, 1e-4f));
        const terrain_sample sample = sample_area(area, point);
        CHECK(near(sample.point.y, box.center.y, 1e-4f));
        CHECK(near(sample.normal, world_up));
    });
    // A ball teed on it sits on its top.
    const float radius = tuning.scale.ball_physics_radius_meters;
    CHECK(near(resting_ball_position(area, hole.tee_position, radius), glm::vec3(0.0f, box.center.y + radius, 0.0f), 1e-4f));

    // Out from each side the bank eases back to the land without a step.
    const float blend = tuning.tee_box.blend_distance;
    const glm::vec3 across(-box.down_hole.z, 0.0f, box.down_hole.x);
    for (const glm::vec3& out : {box.down_hole, -box.down_hole, across, -across}) {
        const float edge = std::abs(glm::dot(out, box.down_hole)) > 0.5f ? box.half_length : box.half_width;
        float previous = ground_height(area, box.center + out * edge);
        for (float distance = edge + 0.1f; distance <= edge + blend + 3.0f; distance += 0.1f) {
            const float height = ground_height(area, box.center + out * distance);
            CHECK(std::abs(height - previous) < 0.04f);  // never steeper than 40%
            previous = height;
        }
        const glm::vec3 beyond = box.center + out * (edge + blend + 3.0f);
        CHECK(near(ground_height(area, beyond), ground_height(land, beyond), 0.02f));
    }
}

TEST_CASE("a teed-up ball rests on the tee box, flush with the ground") {
    const game_state state = started_hole();
    REQUIRE(state.area.tee_boxes.size() == state.course_holes.size());
    const tee_box& box = state.area.tee_boxes[state.ball->hole];
    CHECK(on_tee_box(box, state.ball->position));
    CHECK(near(box.center.y, ground_height(state.area, box.center), 1e-4f));
    CHECK(std::abs(state.ball->position.y - (box.center.y + state.tuning.scale.ball_physics_radius_meters)) < 1e-4f);
    CHECK(on_tee_box(box, state.player.position));
    CHECK(std::abs(state.player.position.y - box.center.y) < 1e-4f);
}
