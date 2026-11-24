#include "doctest.h"

#include "core/render_frame.h"
#include "game/fallen_body.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"
#include "renderer/bag_batch.h"
#include "renderer/marker_scene.h"
#include "renderer/prop_batch.h"
#include "renderer/remote_avatar_batch.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr std::int64_t now_ms = 5000000;

render_data frame_of(const game_state& state) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, text, shipped_content().skills, render_meshes{}, test_props(), 60.0f, true);
}

// A game with one body of mine lying in its place (or `place`), fallen a minute ago.
game_state with_body(const float yaw, const bool bag, const std::string& place = {}) {
    game_state state = started_hole();
    refresh_static_anchor_cache(state);
    state.local_unix_ms = now_ms;
    fallen_body body;
    body.place_id = place.empty() ? state.place.id : place;
    body.position = state.player.position + glm::vec3(2.0f, 0.0f, 3.0f);
    body.position.y = terrain_height(state.area, body.position);
    body.yaw = yaw;
    body.bag_on_back = bag;
    state.save.bodies.push_back(body_fallen_at(body, now_ms - 60000, state.tuning.knockout));
    return state;
}

// What the marker pass draws of the bodies alone.
std::vector<world_marker_vertex> body_vertices(render_data data) {
    data.my_figure.reset();
    data.remote_avatars.clear();
    data.remote_balls.clear();
    data.collectibles.clear();
    data.pin_markers.clear();
    data.show_hole = false;
    world_marker_batch batch;
    const world_marker_scene scene = marker_scene_for(data, 360);
    build_world_marker_batch(batch, scene, test_scene_colors());
    return batch.instanced_vertices();
}

glm::vec3 centroid_of(const std::vector<world_marker_vertex>& vertices, const glm::vec3& colour) {
    glm::vec3 sum(0.0f);
    int count = 0;
    for (const world_marker_vertex& vertex : vertices) {
        if (vertex.color == colour) {
            sum += vertex.position;
            ++count;
        }
    }
    CHECK(count > 0);
    count = std::max(count, 1);
    return sum / static_cast<float>(count);
}

// Where `world` lands on screen (+x right, +y up, in picture heights) in a view from `eye` at `target`.
glm::vec2 on_screen(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& world) {
    const glm::mat4 view = glm::lookAt(eye, target, world_up);
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.05f, 500.0f);
    const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
    CHECK(clip.w > 0.0f);
    return glm::vec2(clip.x * (16.0f / 9.0f), clip.y) / clip.w;  // in picture heights
}
}

TEST_CASE("a body lies in its solid's box on the ground, low, with its head at its yaw's end") {
    for (const float yaw : {0.3f, 0.3f + glm::pi<float>(), -1.2f}) {
        const game_state state = with_body(yaw, false);
        const render_data data = frame_of(state);
        REQUIRE(data.lying_bodies.size() == 1U);
        const fallen_body& body = state.save.bodies.front();
        const solid box = body_solid(body, state.tuning);
        const std::vector<world_marker_vertex> vertices = body_vertices(data);
        const glm::vec3 along = yaw_direction(yaw);
        const glm::vec3 left = yaw_left(along);
        float lowest = 1000.0f;
        float highest = -1000.0f;
        float shortest = 1000.0f;
        float longest = -1000.0f;
        for (const world_marker_vertex& vertex : vertices) {
            const glm::vec3 offset = vertex.position - body.position;
            CHECK(std::abs(glm::dot(offset, along)) <= box.half_length + 0.001f);
            CHECK(std::abs(glm::dot(offset, left)) <= box.half_width + 0.001f);
            lowest = std::min(lowest, offset.y);
            highest = std::max(highest, offset.y);
            shortest = std::min(shortest, glm::dot(offset, along));
            longest = std::max(longest, glm::dot(offset, along));
        }
        // On the ground (level here), no higher than the box, below a standing figure's knee, and nearly as long as one is tall.
        CHECK(near(lowest, 0.0f, 0.02f));
        CHECK(highest <= box.height + 0.001f);
        CHECK(highest < 0.3f * state.tuning.camera.walking_eye_height);
        CHECK(longest - shortest > 0.9f * state.tuning.rangefinder.person_height);

        // The head (skin) is at the +yaw end, the shoes at the other.
        const figure_colors& clothes = test_scene_colors().figures;
        const glm::vec3 head = centroid_of(vertices, clothes.skin);
        const glm::vec3 feet = centroid_of(vertices, clothes.shoes);
        CHECK(glm::dot(head - body.position, along) > 0.4f);
        CHECK(glm::dot(feet - body.position, along) < -0.4f);
    }
}

TEST_CASE("seen through the camera a body lies across the picture with its head on the side it points to") {
    const game_state state = with_body(0.7f, false);
    const render_data data = frame_of(state);
    const fallen_body& body = state.save.bodies.front();
    const std::vector<world_marker_vertex> vertices = body_vertices(data);
    const glm::vec3 along = yaw_direction(body.yaw);
    const glm::vec3 side = yaw_left(along);
    // From beside the middle, just above, looking at it.
    const glm::vec3 target = body.position + glm::vec3(0.0f, 0.1f, 0.0f);
    const glm::vec3 eye = target + side * 5.0f + glm::vec3(0.0f, 0.3f, 0.0f);

    const glm::vec2 middle = on_screen(eye, target, target);
    CHECK(std::abs(middle.x) < 0.01f);
    CHECK(std::abs(middle.y) < 0.01f);
    const glm::vec2 axis_on_screen =
        glm::normalize(on_screen(eye, target, body.position + along) - on_screen(eye, target, body.position - along));
    float along_low = 1000.0f;
    float along_high = -1000.0f;
    float across_low = 1000.0f;
    float across_high = -1000.0f;
    for (const world_marker_vertex& vertex : vertices) {
        const glm::vec2 pixel = on_screen(eye, target, vertex.position) - middle;
        const float a = glm::dot(pixel, axis_on_screen);
        const float c = glm::dot(pixel, glm::vec2(-axis_on_screen.y, axis_on_screen.x));
        along_low = std::min(along_low, a);
        along_high = std::max(along_high, a);
        across_low = std::min(across_low, c);
        across_high = std::max(across_high, c);
    }
    CHECK((along_high - along_low) > 4.0f * (across_high - across_low));

    const figure_colors& clothes = test_scene_colors().figures;
    const glm::vec2 head = on_screen(eye, target, centroid_of(vertices, clothes.skin));
    const glm::vec2 feet = on_screen(eye, target, centroid_of(vertices, clothes.shoes));
    CHECK(glm::dot(head - feet, axis_on_screen) > 0.2f);
    // Flip the yaw and the head is on the other side of the picture.
    const render_data flipped = frame_of(with_body(0.7f + glm::pi<float>(), false));
    const std::vector<world_marker_vertex> turned = body_vertices(flipped);
    CHECK(glm::dot(on_screen(eye, target, centroid_of(turned, clothes.skin)) - on_screen(eye, target, centroid_of(turned, clothes.shoes)),
                   axis_on_screen) < -0.2f);
}

TEST_CASE("a body is drawn only in its place and until it expires") {
    CHECK(frame_of(with_body(0.0f, false)).lying_bodies.size() == 1U);
    CHECK(frame_of(with_body(0.0f, false, "somewhere_else")).lying_bodies.empty());
    game_state expired = with_body(0.0f, false);
    expired.local_unix_ms = expired.save.bodies.front().expires_at_ms;
    CHECK(frame_of(expired).lying_bodies.empty());
    expired.local_unix_ms = expired.save.bodies.front().expires_at_ms - 1;
    CHECK(frame_of(expired).lying_bodies.size() == 1U);
}

TEST_CASE("a bag on its back lies on the ground beside the body, outside its box") {
    const game_state state = with_body(1.1f, true);
    render_data data = frame_of(state);
    REQUIRE(data.lying_bodies.size() == 1U);
    CHECK(!data.lying_bodies.front().bag_model.empty());
    data.carts.clear();
    data.ground_bags.clear();
    data.handled_props.clear();
    prop_batch batch;
    append_carts_and_bags(batch, data, test_props(), test_scene_colors());
    REQUIRE(!batch.opaque_vertices().empty());
    const fallen_body& body = state.save.bodies.front();
    const glm::vec3 left = yaw_left(yaw_direction(body.yaw));
    float lowest = 1000.0f;
    float highest = -1000.0f;
    float nearest = 1000.0f;
    for (const prop_vertex& vertex : batch.opaque_vertices()) {
        const glm::vec3 offset = vertex.position - body.position;
        lowest = std::min(lowest, offset.y);
        highest = std::max(highest, offset.y);
        nearest = std::min(nearest, glm::dot(offset, left));
    }
    CHECK(near(lowest, 0.0f, 0.03f));
    CHECK(highest < 0.5f);
    CHECK(nearest >= state.tuning.knockout.body_half_width_meters - 0.001f);

    CHECK(frame_of(with_body(1.1f, false)).lying_bodies.front().bag_model.empty());
}

TEST_CASE("a body of mine wears the shirt I wear, offline and online, and another's wears theirs") {
    const figure_colors& clothes = test_scene_colors().figures;
    const auto shirt_of_body = [&clothes](const render_data& data) { return remote_tint(clothes, data.lying_bodies.front().account); };

    const render_data offline = frame_of(with_body(0.4f, false));
    REQUIRE(offline.lying_bodies.size() == 1U);
    REQUIRE(offline.my_figure.has_value());
    CHECK(shirt_of_body(offline) == remote_tint(clothes, offline.my_figure->player_id));

    game_state online = started_hole();
    online.play = play_mode::online;
    online.online.status = net_status::connected;
    online.online.account_id = 5;
    fallen_body mine;
    mine.account = 5;
    mine.position = online.player.position + glm::vec3(2.0f, 0.0f, 3.0f);
    fallen_body theirs = mine;
    theirs.account = 6;
    theirs.position += glm::vec3(1.0f, 0.0f, 0.0f);
    online.online.bodies[1] = mine;
    online.online.bodies[2] = theirs;
    const render_data room = frame_of(online);
    REQUIRE(room.lying_bodies.size() == 2U);
    REQUIRE(room.my_figure.has_value());
    CHECK(room.my_figure->player_id == 5U);
    for (const render_lying_body& body : room.lying_bodies) {
        const bool is_mine = body.position == mine.position;
        const glm::vec3 shown = remote_tint(clothes, body.account);
        CHECK(shown == remote_tint(clothes, is_mine ? room.my_figure->player_id : 6U));
    }
}
