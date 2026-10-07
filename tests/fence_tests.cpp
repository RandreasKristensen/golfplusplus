#include "doctest.h"

#include "game/course_world_loader.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/fence_collision.h"
#include "renderer/fence_batch.h"

#include "test_support.h"

#include <algorithm>

namespace {
// A 20 m net along x at z = 0, 10 m high, its feet at y = 0.
const fence_panel net{glm::vec3(-10.0f, 0.0f, 0.0f), glm::vec3(10.0f, 0.0f, 0.0f), 10.0f};

ball_state ball_at(const glm::vec3& position, const glm::vec3& velocity) {
    ball_state ball;
    ball.radius = 0.0214f;
    ball.position = position;
    ball.velocity = velocity;
    return ball;
}
}

TEST_CASE("a fast ball cannot pass through a net between two steps") {
    // 70 m/s for one 1/120 s step: 0.58 m, from one side of the net to the other.
    const glm::vec3 before(0.0f, 3.0f, -0.3f);
    const ball_state after = ball_at(before + glm::vec3(0.0f, 0.0f, 0.58f), glm::vec3(0.0f, 0.0f, 70.0f));
    const ball_state caught = resolve_fence_collision(before, after, net, 0.1f, 0.5f);
    CHECK(caught.position.z < 0.0f);  // stopped on the side it came from
    CHECK(near(caught.position.z, -after.radius, 0.001f));
    CHECK(caught.velocity.z < 0.0f);  // bounced back...
    CHECK(caught.velocity.z > -8.0f);  // ...with little of its speed
}

TEST_CASE("a ball over the net, past its ends or clear of it is left alone") {
    const glm::vec3 before(0.0f, 12.0f, -0.3f);
    const ball_state over = ball_at(before + glm::vec3(0.0f, 0.0f, 0.6f), glm::vec3(0.0f, 0.0f, 70.0f));
    CHECK(resolve_fence_collision(before, over, net, 0.1f, 0.5f).position == over.position);

    const glm::vec3 beside(12.0f, 3.0f, -0.3f);
    const ball_state past_end = ball_at(beside + glm::vec3(0.0f, 0.0f, 0.6f), glm::vec3(0.0f, 0.0f, 70.0f));
    CHECK(resolve_fence_collision(beside, past_end, net, 0.1f, 0.5f).position == past_end.position);

    const glm::vec3 away(0.0f, 3.0f, -2.0f);
    const ball_state clear = ball_at(away + glm::vec3(1.0f, 0.0f, 0.5f), glm::vec3(60.0f, 0.0f, 30.0f));
    CHECK(resolve_fence_collision(away, clear, net, 0.1f, 0.5f).position == clear.position);
}

TEST_CASE("a ball rolling along a net is kept off it") {
    const glm::vec3 before(0.0f, 0.02f, -0.01f);
    const ball_state rolling = ball_at(before + glm::vec3(0.1f, 0.0f, 0.005f), glm::vec3(5.0f, 0.0f, 0.2f));
    const ball_state kept = resolve_fence_collision(before, rolling, net, 0.1f, 0.5f);
    CHECK(kept.position.z <= -rolling.radius + 0.001f);
    CHECK(kept.velocity.z <= 0.0f);
}

TEST_CASE("course worlds carry fences, and refuse a fence that is not one") {
    const course_definition one_hole = course_of({"test"});
    const std::string start = R"("hole_starts": [{"hole_index": 0, "position": [0, 0, 0]}],
        "ground": {"origin": [-50, -50], "cell_size": 50, "columns": 3, "rows": 3, "heights": [0, 0, 0, 0, 0, 0, 0, 0, 0]})";
    const auto with_fences = [&](const std::string& fences) { return "{" + start + fences + "}"; };

    const std::optional<course_world_definition> none = parse_course_world_from_text(with_fences(""), one_hole);
    REQUIRE(none.has_value());
    CHECK(none->fences.empty());

    const std::optional<course_world_definition> fenced = parse_course_world_from_text(
        with_fences(R"(, "fences": [{"poles": [[0, 0, 0], [10, 0, 0], [10, 0, 10]], "height": 12}])"), one_hole);
    REQUIRE(fenced.has_value());
    REQUIRE(fenced->fences.size() == 1U);
    CHECK(fenced->fences[0].poles.size() == 3U);
    CHECK(near(fenced->fences[0].height, 12.0f));

    CHECK(!parse_course_world_from_text(with_fences(R"(, "fences": [{"poles": [[0, 0, 0]], "height": 12}])"), one_hole));
    CHECK(!parse_course_world_from_text(with_fences(R"(, "fences": [{"poles": [[0, 0, 0], [5, 0, 0]], "height": 0}])"),
                                        one_hole));
    CHECK(!parse_course_world_from_text(with_fences(R"(, "fences": [{"poles": [[0, 0, 0], [5, 0, 0]]}])"), one_hole));
}

TEST_CASE("a fence stands on the ground and stops a shot hit into it") {
    game_state state = started_game(fixture_hub_course());
    still_air(state);
    const glm::vec3 tee = state.ball.position;
    // A net across the line of the shot, 30 m out, wide and tall enough to catch it.
    const glm::vec3 forward = yaw_direction(state.aim_angle);
    const glm::vec3 side = yaw_left(forward);
    const glm::vec3 middle = tee + forward * 30.0f;
    course_world_fence fence{{middle - side * 40.0f, middle + side * 40.0f}, 40.0f};
    course_world_definition world = state.hub->world;
    world.fences = {fence};
    state.area = build_course_area(state.course_holes, world, state.tuning);
    refresh_static_anchor_cache(state);

    REQUIRE(state.area.fence_panels.size() == 1U);
    REQUIRE(state.area.fence_poles.size() == 2U);
    CHECK(near(state.area.fence_poles[0].base.y, terrain_height(state.area, fence.poles[0]), 0.001f));

    shot_input input;
    input.ball_start = tee;
    input.aim_angle = state.aim_angle;
    input.club_id = state.clubs.front().id;
    input.power = 1.0f;
    const shot_result result = simulate_shot(input, current_shot_course(state), state.tuning, state.clubs, state.rewards);
    // It rests short of the net, on the tee's side.
    CHECK(glm::dot(result.rest_position - middle, forward) < 0.0f);
}

TEST_CASE("a fence is drawn as a post at every pole and a net between each pair") {
    play_area area;
    area.fences = {course_world_fence{{glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(10.0f, 1.0f, 4.0f)}, 12.0f}};
    const render_fences fences = build_render_fences(area, 7);
    CHECK(fences.revision == 7U);
    CHECK(fences.posts.size() == 3U * 4U * 6U);  // three posts, four sides, two triangles a side
    REQUIRE(fences.nets.size() == 2U * 6U);
    // The net's texture runs on across the middle pole, and up its height.
    float second_start = 1e9f;
    float top = 0.0f;
    for (std::size_t i = 6; i < 12; ++i) {
        second_start = std::min(second_start, fences.nets[i].uv.x);
        top = std::max(top, fences.nets[i].uv.y);
    }
    CHECK(near(second_start, 10.0f / net_tile_metres));
    CHECK(near(top, 12.0f / net_tile_metres));
    CHECK(build_render_fences(play_area{}, 1).nets.empty());
}
