#include "doctest.h"

#include "core/render_frame.h"
#include "core/scene_signs.h"
#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/hole_sign_face.h"
#include "renderer/map_ink.h"
#include "renderer/overlay_raster.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

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
    return hole_area(hole, shipped_content().tuning);
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
    const play_area area = area_for(hole);
    REQUIRE(area.signs.size() == 1U);
    const hole_sign& sign = area.signs.front();
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

TEST_CASE("every hole of a course gets a sign at its placed tee, and posts that stop shots") {
    const game_state state = started_game(fixture_hub_course());
    REQUIRE(course_started(state));
    REQUIRE(state.area.signs.size() == state.course_holes.size());
    for (std::size_t i = 0; i < state.area.signs.size(); ++i) {
        const hole_sign& sign = state.area.signs[i];
        CHECK(sign.area_hole == i);
        CHECK(solid_stands_at(state.area, solid_kind::pole, sign.post_feet[0]));
        CHECK(solid_stands_at(state.area, solid_kind::pole, sign.post_feet[1]));
        CHECK(near(sign.line_of_play.front(), state.holes[i].tee_position, 0.001f));
        CHECK(on_tee_box(state.area.tee_boxes[i], sign.post_feet[0]));
        CHECK(on_tee_box(state.area.tee_boxes[i], sign.post_feet[1]));
    }
}

TEST_CASE("a ball bounces off a hole sign's post") {
    game_state state = started_hole();
    play_hole(state, wide_hole(4.0f, 4.0f));
    still_air(state);
    const std::vector<solid> poles = solids_of(state.area, solid_kind::pole);
    const hole_sign& sign = state.area.signs.front();
    const auto found = std::find_if(poles.begin(), poles.end(), [&](const solid& pole) {
        return horizontal_distance(pole.base, sign.post_feet[0]) < 0.001f;
    });
    REQUIRE(found != poles.end());
    const solid& post = *found;
    CHECK(post.stops_ball);
    CHECK(near(post.base.y + post.height, hole_sign_top(sign)));

    ball_state ball = still_ball(state);
    ball.radius = 0.1f;
    ball.position = post.base + glm::vec3(0.0f, 0.5f, -0.12f);
    ball.velocity = glm::vec3(0.0f, 0.0f, 3.0f);
    const shot_step step =
        step_shot(ball, sample_area(state.area, ball.position), current_shot_course(state), state.tuning, 0.016f);
    CHECK(step.hit == shot_event_kind::pole_hit);
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
    draw_hole_sign_face(batch, text, make_hole_sign_labels(text, 18, 5, 999, false), area, area.signs.front(),
                        test_scene_colors().map);
    CHECK(batch.truncated_text_count == 0U);
    const rgba_image face = rasterize_overlay_batch(batch, test_props().at("hole_sign").color("board"));
    CHECK(face.width == hole_sign_face_width);
    CHECK(face.height == hole_sign_face_height);
    CHECK(count_texels(face, map_material_ink(test_scene_colors().map, terrain_material::fairway)) > 20U);
    CHECK(count_texels(face, map_material_ink(test_scene_colors().map, terrain_material::green)) > 4U);
}

TEST_CASE("render hole signs are rebuilt only when the signs change") {
    game_state state = started_game(fixture_hub_course());
    const text_assets& text = shipped_text_assets();
    render_hole_signs signs;
    CHECK(refresh_render_hole_signs(signs, state.area, state.terrain_render_revision, hole_sign_texts(state), {}, text, test_scene_colors(), test_props().at("hole_sign"), 1));
    CHECK(signs.revision == 1U);
    REQUIRE(tee_up(state, 1, false));
    CHECK(!refresh_render_hole_signs(signs, state.area, state.terrain_render_revision, hole_sign_texts(state), {}, text, test_scene_colors(), test_props().at("hole_sign"), 2));
    CHECK(signs.revision == 1U);

    std::vector<hole_sign_text> renumbered = hole_sign_texts(state);
    renumbered.front().par += 1;
    CHECK(refresh_render_hole_signs(signs, state.area, state.terrain_render_revision, renumbered, {}, text, test_scene_colors(), test_props().at("hole_sign"), 3));
    CHECK(signs.revision == 3U);
}

TEST_CASE("the scene paints its sign faces again when the course loads again, not on a hole change") {
    const text_assets& text = shipped_text_assets();
    const startup_catalog catalog;
    scene_colors colors = test_scene_colors();
    game_state state = started_game(fixture_hub_course());
    render_hole_signs signs;
    REQUIRE(refresh_scene_hole_signs(signs, state, catalog, text, colors, test_props(), ""));
    const std::uint64_t first_upload = signs.revision;
    REQUIRE(!signs.signs.empty());
    const rgba_image first_face = signs.signs.front().face;

    // Frames and normal hole transitions leave the upload alone.
    CHECK(!refresh_scene_hole_signs(signs, state, catalog, text, colors, test_props(), ""));
    REQUIRE(tee_up(state, 1, false));
    CHECK(!refresh_scene_hole_signs(signs, state, catalog, text, colors, test_props(), ""));
    CHECK(signs.revision == first_upload);

    // The same course again with other map inks (the tour's F2 edit).
    colors.map.fairway = glm::vec3(0.9f, 0.1f, 0.1f);
    colors.map.rough = glm::vec3(0.1f, 0.1f, 0.9f);
    REQUIRE(start_course(state, fixture_hub_course()));
    CHECK(refresh_scene_hole_signs(signs, state, catalog, text, colors, test_props(), ""));
    CHECK(signs.revision == first_upload + 1);
    CHECK(signs.terrain_revision == state.terrain_render_revision);
    CHECK(signs.signs.front().face.pixels != first_face.pixels);
    CHECK(!refresh_scene_hole_signs(signs, state, catalog, text, colors, test_props(), ""));
}

TEST_CASE("the scene's signs follow a board turned by a course load, though its centre and text are the same") {
    const text_assets& text = shipped_text_assets();
    const startup_catalog catalog;
    game_state state = started_game(fixture_hub_course());
    render_hole_signs signs;
    REQUIRE(refresh_scene_hole_signs(signs, state, catalog, text, test_scene_colors(), test_props(), ""));
    const std::vector<hole_sign_vertex> before = signs.vertices;

    state.area.signs.front().face_normal = -state.area.signs.front().face_normal;
    CHECK(!refresh_scene_hole_signs(signs, state, catalog, text, test_scene_colors(), test_props(), ""));
    mark_terrain_render_dirty(state);
    CHECK(refresh_scene_hole_signs(signs, state, catalog, text, test_scene_colors(), test_props(), ""));
    REQUIRE(signs.vertices.size() == before.size());
    CHECK(signs.vertices.front().normal == -before.front().normal);
}

TEST_CASE("render hole signs carry a face and a model per sign, numbered by course hole") {
    game_state state = started_game(fixture_hub_course());
    const render_hole_signs signs =
        build_render_hole_signs(state.area, hole_sign_texts(state), {}, shipped_text_assets(), test_scene_colors(), test_props().at("hole_sign"), 7);
    CHECK(signs.revision == 7U);
    // The hole signs, then each way sign's boards.
    std::size_t way_boards = 0;
    for (const way_sign& sign : state.area.way_signs) {
        way_boards += sign.boards.size();
    }
    REQUIRE(signs.signs.size() == state.area.signs.size() + way_boards);
    CHECK(signs.truncated_text_count == 0U);
    std::size_t next_vertex = 0;
    for (std::size_t i = 0; i < signs.signs.size(); ++i) {
        const render_hole_sign& sign = signs.signs[i];
        CHECK(sign.first_vertex == next_vertex);
        CHECK(sign.vertex_count > 0U);
        CHECK(sign.vertex_count % 3U == 0U);
        if (i < state.area.signs.size()) {
            CHECK(sign.face.width == hole_sign_face_width);
        } else {
            CHECK(sign.face.height == way_sign_face_height);
        }
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

TEST_CASE("a way board's pointed end runs on from the end of its face, flush with it, and both faces carry the texture") {
    way_sign sign;
    sign.post_foot = glm::vec3(10.0f, 2.0f, -5.0f);
    sign.board_half_height = 0.2f;
    sign.board_thickness = 0.05f;
    sign.post_radius = 0.06f;
    way_board board;
    board.half_width = 0.5f;
    board.points = glm::normalize(glm::vec3(3.0f, 0.0f, 4.0f));
    board.face_normal = glm::vec3(-board.points.z, 0.0f, board.points.x);
    board.center = sign.post_foot + board.points * board.half_width + glm::vec3(0.0f, 1.0f, 0.0f);
    sign.boards = {board};
    std::vector<hole_sign_vertex> vertices;
    append_way_board_model(vertices, sign, 0, test_props().at("hole_sign"));

    float face_end = -1.0f;
    std::size_t textured = 0;
    for (const hole_sign_vertex& vertex : vertices) {
        if (vertex.textured > 0.5f) {
            ++textured;
            // On one of the two faces, either side of the board's middle.
            CHECK(std::abs(std::abs(glm::dot(vertex.position - board.center, board.face_normal)) - sign.board_thickness * 0.5f) <
                  0.0001f);
            face_end = std::max(face_end, glm::dot(vertex.position - board.center, board.points));
        }
    }
    CHECK(textured == 12U);
    CHECK(std::abs(face_end - board.half_width) < 0.0001f);
    // Both of the face's end corners are corners of the pointed end, whose tip
    // lies on in the face's plane.
    const glm::vec3 front = board.center + board.face_normal * (sign.board_thickness * 0.5f);
    const glm::vec3 end = front + board.points * board.half_width;
    const glm::vec3 tip = end + board.points * sign.board_half_height;
    for (const glm::vec3& corner : {end + glm::vec3(0.0f, sign.board_half_height, 0.0f), end - glm::vec3(0.0f, sign.board_half_height, 0.0f), tip}) {
        const bool shared = std::any_of(vertices.begin(), vertices.end(), [&](const hole_sign_vertex& vertex) {
            return vertex.textured < 0.5f && glm::length(vertex.position - corner) < 0.0001f;
        });
        CHECK(shared);
    }
    // The tip is the board's half width twice and its half height from the post's axis.
    CHECK(near(way_board_tip(sign, board), sign.post_foot + glm::vec3(0.0f, 1.0f, 0.0f) + board.points * 1.2f, 0.0001f));
}

namespace {
// A board saying `to_holes`, built as place_way_signs builds it, and its face's width in texels.
struct lettered_board {
    float half_width = 0.0f;
    float length = 0.0f;
    int face_width = 0;
    text_layout layout;
};

lettered_board lettered(const std::vector<std::size_t>& to_holes) {
    const way_sign_tuning& tuning = shipped_content().tuning.way_sign;
    const float half_height = 0.5f * tuning.board_height;
    lettered_board board;
    board.length = way_board_length(tuning, way_sign_label(to_holes));
    board.half_width = 0.5f * (board.length - half_height);
    board.face_width = way_sign_face_width(board.half_width, half_height);
    board.layout = layout_way_sign_label(shipped_text_assets(), way_sign_label(to_holes), board.face_width);
    return board;
}

// The lettering's height in texels: the font's rows at the layout's scale.
int glyph_texels(const text_layout& layout) {
    return layout.scale * shipped_text_assets().font.height;
}
}

TEST_CASE("a way board says the numbers of the tees it points to") {
    CHECK(way_sign_label({1U}) == "2");
    CHECK(way_sign_label({1U, 6U}) == "2  7");
    CHECK(way_sign_label({9U, 10U}) == "10  11");
}

TEST_CASE("every way board's lettering is the same size and texel density whatever it says; the board is as long as its label needs") {
    const way_sign_tuning& tuning = shipped_content().tuning.way_sign;
    const lettered_board one = lettered({1U});
    const lettered_board two_digits = lettered({9U});
    const lettered_board two_numbers = lettered({1U, 6U});
    const lettered_board widest = lettered({9U, 10U});
    const lettered_board three = lettered({1U, 8U, 17U});
    for (const lettered_board* board : {&one, &two_digits, &two_numbers, &widest, &three}) {
        CHECK(!board->layout.truncated);
        CHECK(board->layout.scale == one.layout.scale);
        CHECK(glyph_texels(board->layout) == glyph_texels(one.layout));
        // Texels per metre along the board and up it.
        CHECK(near(static_cast<float>(board->face_width) / (2.0f * board->half_width),
                   static_cast<float>(way_sign_face_height) / tuning.board_height, 0.01f));
    }
    CHECK(one.layout.scale >= 2);
    CHECK(near(one.length, tuning.board_length, 0.0001f));
    CHECK(two_digits.length > one.length);
    CHECK(two_numbers.length > two_digits.length);
    CHECK(widest.length > two_numbers.length);
    CHECK(three.length > two_numbers.length);
    CHECK(widest.face_width > one.face_width);
    // Each board is wider than its lettering with the frame to spare.
    for (const lettered_board* board : {&one, &widest, &three}) {
        CHECK(rect_left(board->layout.bounds) > -1.0f + 2.0f * 3.0f / static_cast<float>(board->face_width) - 0.0001f);
        CHECK(rect_right(board->layout.bounds) < 1.0f - 2.0f * 3.0f / static_cast<float>(board->face_width) + 0.0001f);
    }
    overlay_batch batch;
    draw_way_sign_face(batch, shipped_text_assets(), way_sign_label({9U, 10U}), widest.face_width);
    CHECK(batch.truncated_text_count == 0U);
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

TEST_CASE("a hole sign's map is framed on its hole alone and shows the whole course in that frame") {
    hole_data hole = wide_hole(20.0f, 40.0f);
    hole.material_zones = {circle_zone(material_zone_type::green, hole.pin_position, 12.0f)};
    const hole_sign_map_frame plain = make_hole_sign_map_frame(area_for(hole), area_for(hole).signs.front());

    // Its own bunker and pond far off the hole stretch nothing.
    hole.material_zones.push_back(circle_zone(material_zone_type::bunker, glm::vec3(150.0f, 0.0f, 60.0f), 10.0f));
    hole.material_zones.push_back(circle_zone(material_zone_type::water, glm::vec3(-400.0f, 0.0f, 60.0f), 60.0f));
    const play_area area = area_for(hole);
    const hole_sign& sign = area.signs.front();
    const hole_sign_map_frame frame = make_hole_sign_map_frame(area, sign);
    CHECK(near(frame.origin, plain.origin));
    CHECK(near(frame.texels_per_unit, plain.texels_per_unit));

    // Another hole crossing it with its green, and that hole's bunker on this
    // one's fairway.
    play_area crowded = area;
    terrain_mesh other = area.holes.front();
    for (terrain_vertex& vertex : other.vertices) {
        vertex.position = glm::vec3(vertex.position.z - 60.0f, vertex.position.y, vertex.position.x + 30.0f);
    }
    crowded.holes.push_back(build_terrain_mesh_index(std::move(other)));
    crowded.zones.push_back(circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 90.0f), 6.0f));
    crowded.zones.push_back(circle_zone(material_zone_type::green, glm::vec3(-28.0f, 0.0f, 30.0f), 4.0f));
    // The first hole's zones, then its bunker; the green is the other hole's.
    crowded.zone_holes.assign(crowded.zones.size() - 1, 0);
    crowded.zone_holes.push_back(1);
    crowded.trees.push_back(tree_instance{glm::vec3(5.0f, 0.0f, 40.0f), tree_shape{}});
    CHECK(near(make_hole_sign_map_frame(crowded, sign).origin, frame.origin));
    const auto spot = [&](const glm::vec3& point) { return hole_sign_map_spot_at(crowded, sign, point); };
    CHECK(!spot(glm::vec3(0.0f, 0.0f, -200.0f)).has_value());
    REQUIRE(spot(glm::vec3(0.0f, 0.0f, 90.0f)).has_value());
    CHECK(spot(glm::vec3(0.0f, 0.0f, 90.0f))->material == terrain_material::bunker);

    // Only the sign's own hole is in full ink: the other's fairway and green
    // are faded, on its own and on the map.
    const map_inks& inks = test_scene_colors().map;
    const std::optional<hole_sign_map_spot> own_fairway = spot(glm::vec3(0.0f, 0.0f, 60.0f));
    const std::optional<hole_sign_map_spot> other_fairway = spot(glm::vec3(-28.0f, 0.0f, 38.0f));
    const std::optional<hole_sign_map_spot> own_green = spot(hole.pin_position);
    const std::optional<hole_sign_map_spot> other_green = spot(glm::vec3(-28.0f, 0.0f, 30.0f));
    REQUIRE((own_fairway && other_fairway && own_green && other_green));
    CHECK((own_fairway->material == terrain_material::fairway && other_fairway->material == terrain_material::fairway));
    CHECK((own_green->material == terrain_material::green && other_green->material == terrain_material::green));
    CHECK((own_fairway->own_hole && own_green->own_hole));
    CHECK((!other_fairway->own_hole && !other_green->own_hole));
    CHECK(hole_sign_map_ink(inks, *own_fairway) != hole_sign_map_ink(inks, *other_fairway));
    CHECK(hole_sign_map_ink(inks, *own_green) != hole_sign_map_ink(inks, *other_green));

    const text_assets& text = shipped_text_assets();
    overlay_batch batch;
    draw_hole_sign_face(batch, text, make_hole_sign_labels(text, 1, 4, 120, true), crowded, sign, inks);
    const rgba_image drawn = rasterize_overlay_batch(batch, test_props().at("hole_sign").color("board"));
    CHECK(count_texels(drawn, map_material_ink(inks, terrain_material::bunker)) > 0U);
    CHECK(count_texels(drawn, inks.tree) == 1U);
    CHECK(count_texels(drawn, hole_sign_map_ink(inks, *own_fairway)) > 20U);
    CHECK(count_texels(drawn, hole_sign_map_ink(inks, *other_fairway)) > 20U);
    CHECK(count_texels(drawn, hole_sign_map_ink(inks, *other_green)) > 0U);
}
