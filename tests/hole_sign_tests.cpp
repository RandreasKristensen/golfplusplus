#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"
#include "renderer/course_map_fill.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/hole_sign_face.h"
#include "renderer/overlay_raster.h"
#include "renderer/terrain_palette.h"

#include "test_support.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/geometric.hpp>

namespace {
// The player's right looking along `forward`.
glm::vec3 right_of(const glm::vec3& forward) {
    return glm::vec3(-forward.z, 0.0f, forward.x);
}

// A straight hole up +Z with the given fairway and rough widths.
hole_data wide_hole(const float fairway, const float rough) {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 120.0f), fairway);
    hole.spline.rough_width = rough;
    return hole;
}

play_area area_for(const hole_data& hole) {
    return build_hole_area(hole, shipped_content().tuning);
}

const hole_sign_tuning& sign_tuning() {
    return shipped_content().tuning.hole_sign;
}


std::size_t count_texels(const rgba_image& image, const glm::vec3& color) {
    std::size_t count = 0;
    const auto byte = [](const float value) { return static_cast<std::uint8_t>(std::lround(value * 255.0f)); };
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        if (image.pixels[i] == byte(color.r) && image.pixels[i + 1] == byte(color.g) && image.pixels[i + 2] == byte(color.b)) {
            ++count;
        }
    }
    return count;
}
}

TEST_CASE("a hole sign stands on its tee box's tiles at the right edge, along the hole") {
    const hole_data hole = wide_hole(20.0f, 40.0f);
    const play_area area = area_for(hole);
    REQUIRE(area.signs.size() == 1U);
    REQUIRE(area.tee_boxes.size() == 1U);
    const hole_sign& sign = area.signs.front();
    const tee_box& box = area.tee_boxes.front();

    CHECK(near(sign.down_hole, glm::vec3(0.0f, 0.0f, 1.0f)));
    // Looking up +Z the right is -X; the face looks back across the tee.
    const glm::vec3 right = right_of(sign.down_hole);
    CHECK(near(right, glm::vec3(-1.0f, 0.0f, 0.0f)));
    CHECK(near(sign.face_normal, -right));
    CHECK(near(glm::dot(sign.face_normal, sign.down_hole), 0.0f));

    const glm::vec3 offset = horizontal(sign.board_center - hole.tee_position);
    CHECK(near(glm::dot(offset, right), box.half_width - sign_tuning().edge_inset, 0.001f));
    CHECK(near(glm::dot(offset, sign.down_hole), sign_tuning().forward_offset, 0.001f));
    CHECK(glm::dot(hole.tee_position - sign.board_center, sign.face_normal) > 0.0f);

    // Both posts on the box, standing on its top.
    for (const glm::vec3& foot : sign.post_feet) {
        CHECK(on_tee_box(box, foot));
        CHECK(near(foot.y, box.center.y, 0.0001f));
    }
    // Posts at the board's two ends, behind it, holding it up.
    CHECK(glm::dot(sign.post_feet[1] - sign.post_feet[0], sign.down_hole) > 0.0f);
    CHECK(glm::dot(horizontal(sign.post_feet[0] - sign.board_center), sign.face_normal) < 0.0f);
    const float bottom = sign.board_center.y - sign.board_half_height;
    CHECK(near(bottom, box.center.y + sign_tuning().board_lift, 0.0001f));
    CHECK(near(sign.board_half_width * 2.0f, sign_tuning().board_width));
}

TEST_CASE("a hole sign faces along the hole's line near the tee, not the pin") {
    hole_data hole = wide_hole(4.0f, 4.0f);
    // A dogleg: straight up +Z for 100, then hard to +X.
    hole.spline.control_points = {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(100.0f, 0.0f, 100.0f)};
    hole.pin_position = glm::vec3(100.0f, 0.0f, 100.0f);
    const hole_sign& sign = area_for(hole).signs.front();
    CHECK(near(sign.down_hole, glm::vec3(0.0f, 0.0f, 1.0f), 0.0001f));
    CHECK(near(sign.length, 200.0f, 0.001f));
}

TEST_CASE("the line of play runs tee, interior control points, pin") {
    hole_data hole = straight_hole(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 50.0f), 10.0f);
    hole.spline.control_points = {glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 20.0f), glm::vec3(0.0f, 0.0f, 49.0f)};
    const std::vector<glm::vec3> line = line_of_play(hole);
    REQUIRE(line.size() == 3U);
    CHECK(near(line[0], hole.tee_position));
    CHECK(near(line[1], glm::vec3(3.0f, 0.0f, 20.0f)));
    CHECK(near(line[2], hole.pin_position));
    CHECK(near(point_along(line, 0.0f), line[0]));
    CHECK(near(point_along(line, 1000.0f), line[2]));
    CHECK(near(polyline_length({glm::vec3(0.0f), glm::vec3(3.0f, 7.0f, 4.0f)}), 5.0f));
}

TEST_CASE("every hole of a hub course gets a sign at its placed tee, and posts that stop shots") {
    const game_state state = started_game(fixture_hub_course());
    REQUIRE(state.hub);
    REQUIRE(state.area.signs.size() == state.course_holes.size());
    CHECK(state.area.sign_posts.size() == state.area.signs.size() * 2U);
    for (std::size_t i = 0; i < state.area.signs.size(); ++i) {
        const hole_sign& sign = state.area.signs[i];
        CHECK(sign.area_hole == i);
        CHECK(near(sign.line_of_play.front(), state.hub->markers[i].tee_position, 0.001f));
        CHECK(on_tee_box(state.area.tee_boxes[i], sign.post_feet[0]));
        CHECK(on_tee_box(state.area.tee_boxes[i], sign.post_feet[1]));
    }
}

TEST_CASE("a ball bounces off a hole sign's post") {
    game_state state = started_hole();
    play_hole(state, wide_hole(4.0f, 4.0f));
    still_air(state);
    REQUIRE(state.area.sign_posts.size() == 2U);
    const tree_body& post = state.area.sign_posts.front();
    CHECK(post.shape.leaf_height == 0.0f);
    CHECK(near(post.base.y + post.shape.trunk_height, hole_sign_top(state.area.signs.front())));

    ball_state ball = state.ball;
    ball.radius = 0.1f;
    ball.position = post.base + glm::vec3(0.0f, 0.5f, -0.12f);
    ball.velocity = glm::vec3(0.0f, 0.0f, 3.0f);
    const shot_step step = step_shot(ball, sample_area(state.area, ball.position), current_shot_course(state),
                                     state.clubs.front().stats, state.tuning, 0.0f, 0.016f);
    CHECK(step.hit_tree);
    CHECK(step.ball.velocity.z < 0.0f);
}

TEST_CASE("overlay raster paints each texel once where quads share an edge") {
    overlay_batch batch;
    batch.grid = overlay_grid{8, 4};
    draw_overlay_quad(batch, glm::vec2(-0.5f, 0.0f), glm::vec2(0.5f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.5f);
    draw_overlay_quad(batch, glm::vec2(0.5f, 0.0f), glm::vec2(0.5f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.5f);
    const rgba_image image = rasterize_overlay_batch(batch, glm::vec3(0.0f));
    REQUIRE(image.width == 8);
    REQUIRE(image.height == 4);
    CHECK(count_texels(image, glm::vec3(128.0f / 255.0f, 0.0f, 0.0f)) == 32U);
    CHECK(image.pixels[3] == 255U);
}

TEST_CASE("overlay raster puts clip y = -1 on row 0") {
    overlay_batch batch;
    batch.grid = overlay_grid{2, 2};
    draw_overlay_quad(batch, glm::vec2(0.0f, -0.5f), glm::vec2(1.0f, 0.5f), glm::vec3(1.0f));
    const rgba_image image = rasterize_overlay_batch(batch, glm::vec3(0.0f));
    CHECK(image.pixels[0] == 255U);
    CHECK(image.pixels[(1 * 2 + 0) * 4] == 0U);
}

TEST_CASE("a hole sign's face shows the hole in map inks and its labels fit") {
    hole_data hole = wide_hole(20.0f, 40.0f);
    hole.material_zones = {circle_zone(material_zone_type::green, hole.pin_position, 12.0f)};
    const play_area area = area_for(hole);
    const text_assets& text = shipped_text_assets();

    overlay_batch batch;
    draw_hole_sign_face(batch, text, make_hole_sign_labels(text, 18, 5, 999), area, area.signs.front());
    CHECK(batch.truncated_text_count == 0U);
    const rgba_image face = rasterize_overlay_batch(batch, hole_sign_wood);
    CHECK(face.width == hole_sign_face_width);
    CHECK(face.height == hole_sign_face_height);
    CHECK(count_texels(face, course_map_ink(terrain_material_color(terrain_material::fairway))) > 20U);
    CHECK(count_texels(face, course_map_ink(terrain_material_color(terrain_material::green))) > 4U);
}

TEST_CASE("render hole signs are rebuilt only when the signs change") {
    game_state state = started_game(fixture_hub_course());
    render_hole_signs signs;
    CHECK(refresh_render_hole_signs(signs, state, shipped_text_assets(), 1));
    CHECK(signs.revision == 1U);
    REQUIRE(start_hub_hole(state, 1));
    CHECK(!refresh_render_hole_signs(signs, state, shipped_text_assets(), 2));
    CHECK(signs.revision == 1U);

    game_state linear = started_hole();
    CHECK(refresh_render_hole_signs(signs, linear, shipped_text_assets(), 3));
    CHECK(signs.revision == 3U);
}

TEST_CASE("render hole signs carry a face and a model per sign, numbered by course hole") {
    game_state state = started_game(fixture_hub_course());
    const render_hole_signs signs = build_render_hole_signs(state, shipped_text_assets(), 7);
    CHECK(signs.revision == 7U);
    REQUIRE(signs.signs.size() == state.area.signs.size());
    CHECK(signs.truncated_text_count == 0U);
    std::size_t next_vertex = 0;
    for (const render_hole_sign& sign : signs.signs) {
        CHECK(sign.first_vertex == next_vertex);
        CHECK(sign.vertex_count > 0U);
        CHECK(sign.vertex_count % 3U == 0U);
        CHECK(sign.face.width == hole_sign_face_width);
        next_vertex += sign.vertex_count;
    }
    CHECK(next_vertex == signs.vertices.size());

    // The face is the only textured side, and faces across the tee.
    const hole_sign& placed = state.area.signs.front();
    std::size_t textured = 0;
    for (std::size_t i = 0; i < signs.signs.front().vertex_count; ++i) {
        const hole_sign_vertex& vertex = signs.vertices[i];
        if (vertex.textured > 0.5f) {
            ++textured;
            CHECK(near(vertex.normal, placed.face_normal));
        }
    }
    CHECK(textured == 6U);
}


TEST_CASE("a hole sign's map runs the hole across the face, tee on the right and pin on the left, fitted to the box") {
    hole_data hole = wide_hole(20.0f, 40.0f);
    hole.material_zones = {circle_zone(material_zone_type::green, hole.pin_position, 12.0f)};
    const play_area area = area_for(hole);
    const hole_sign& sign = area.signs.front();
    const hole_sign_map_frame frame = make_hole_sign_map_frame(area, sign);

    const glm::ivec2 tee = frame.texel(hole.tee_position);
    const glm::ivec2 pin = frame.texel(hole.pin_position);
    CHECK(tee.x > hole_sign_map_width * 3 / 4);
    CHECK(pin.x < hole_sign_map_width / 4);
    CHECK(std::abs(tee.y - pin.y) <= 1);

    // Every point of the hole's ribbon is on the map, and the hole spans it
    // along one axis less only the margin.
    glm::ivec2 low(hole_sign_map_width, hole_sign_map_height);
    glm::ivec2 high(-1);
    for (const terrain_vertex& vertex : area.holes.front().vertices) {
        const glm::ivec2 texel = frame.texel(vertex.position);
        low = glm::min(low, texel);
        high = glm::max(high, texel);
    }
    CHECK(low.x >= 0);
    CHECK(low.y >= 0);
    CHECK(high.x < hole_sign_map_width);
    CHECK(high.y < hole_sign_map_height);
    CHECK((high.x - low.x >= hole_sign_map_width - 6 || high.y - low.y >= hole_sign_map_height - 6));
}

TEST_CASE("a hole sign's map shows only its own hole") {
    hole_data hole = wide_hole(20.0f, 40.0f);
    const glm::vec3 pond(0.0f, 0.0f, 60.0f);
    hole.material_zones = {circle_zone(material_zone_type::green, hole.pin_position, 12.0f),
                           circle_zone(material_zone_type::water, pond, 8.0f)};
    const play_area area = area_for(hole);
    const hole_sign& sign = area.signs.front();
    const text_assets& text = shipped_text_assets();
    const hole_sign_labels labels = make_hole_sign_labels(text, 1, 4, 120);
    overlay_batch batch;
    draw_hole_sign_face(batch, text, labels, area, sign);
    const rgba_image own = rasterize_overlay_batch(batch, hole_sign_wood);
    CHECK(count_texels(own, course_map_material_ink(terrain_material::water)) > 4U);

    // Another hole crossing it, and that hole's bunker on this one's fairway.
    play_area crowded = area;
    terrain_mesh other = area.holes.front();
    for (terrain_vertex& vertex : other.vertices) {
        vertex.position = glm::vec3(vertex.position.z - 60.0f, vertex.position.y, vertex.position.x + 30.0f);
    }
    crowded.holes.push_back(build_terrain_mesh_index(std::move(other)));
    crowded.zones.push_back(circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 90.0f), 6.0f));
    clear_overlay_batch(batch);
    draw_hole_sign_face(batch, text, labels, crowded, sign);
    const rgba_image drawn = rasterize_overlay_batch(batch, hole_sign_wood);
    CHECK(drawn.pixels == own.pixels);
    CHECK(count_texels(drawn, course_map_material_ink(terrain_material::bunker)) == 0U);
    CHECK(!hole_sign_map_material(crowded, sign, glm::vec3(-60.0f, 0.0f, 30.0f)).has_value());
    CHECK(hole_sign_map_material(crowded, sign, pond) == terrain_material::water);
}
