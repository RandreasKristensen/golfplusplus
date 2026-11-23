#include "doctest.h"

#include "game/text_ids.h"
#include "renderer/course_map_card.h"
#include "renderer/course_map_layout.h"
#include "renderer/course_map_overlay.h"
#include "renderer/map_ink.h"
#include "renderer/overlay_batch.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>

namespace {
const map_inks& inks() {
    return test_scene_colors().map;
}

// The four corners of an XZ box, as a course's outline.
std::vector<glm::vec3> box_outline(const glm::vec3& low, const glm::vec3& high) {
    return {low, glm::vec3(high.x, 0.0f, low.z), high, glm::vec3(low.x, 0.0f, high.z)};
}
}

TEST_CASE("the course map fits the course's holes to its paper, square on screen, at every aspect") {
    render_data data;
    data.course_map_outline = box_outline(glm::vec3(-100.0f, 0.0f, 50.0f), glm::vec3(300.0f, 0.0f, 950.0f));
    data.player_position = glm::vec3(0.0f, 0.0f, 100.0f);
    for (const overlay_grid grid : {overlay_grid{554, 416}, overlay_grid{640, 360}, overlay_grid{733, 314}}) {
        const course_map_layout layout = make_course_map_layout(data, grid);
        // One world unit is as many pixels across as up.
        CHECK(std::abs(layout.scale.x * static_cast<float>(grid.width) - layout.scale.y * static_cast<float>(grid.height)) <
              0.0001f);
        glm::vec2 low(1.0e9f);
        glm::vec2 high(-1.0e9f);
        for (const glm::vec3& corner : data.course_map_outline) {
            low = glm::min(low, map_point(layout, corner));
            high = glm::max(high, map_point(layout, corner));
        }
        const glm::vec2 paper_low = layout.center - layout.half_size;
        const glm::vec2 paper_high = layout.center + layout.half_size;
        CHECK(low.x > paper_low.x);
        CHECK(low.y > paper_low.y);
        CHECK(high.x < paper_high.x);
        CHECK(high.y < paper_high.y);
        // The course fills the paper along its longer side, less a small margin.
        const glm::vec2 fill = (high - low) / (layout.half_size * 2.0f);
        CHECK(std::max(fill.x, fill.y) > 0.8f);
        // Its 900 m way lies across the paper, its 400 m way up it.
        const glm::vec2 pixels = (high - low) * glm::vec2(static_cast<float>(grid.width), static_cast<float>(grid.height));
        CHECK(pixels.x > pixels.y * 2.0f);
    }
}

// The same points and angle as MapTurnTests in tooling/osm_import/test_osm_golf_convert.py, with
// the paper's drawable part in paper heights (osm_ground.MAP_DRAWABLE).
TEST_CASE("the course map's turn is the angle of the largest scale, the one the importer chooses") {
    const std::vector<glm::vec3> points = [] {
        std::vector<glm::vec3> made;
        for (const glm::vec2 xz : {glm::vec2(0, 0), glm::vec2(900, 500), glm::vec2(1400, 1200), glm::vec2(400, 200),
                                   glm::vec2(1000, 0), glm::vec2(100, 600), glm::vec2(1250, 900), glm::vec2(-200, 350),
                                   glm::vec2(700, 820), glm::vec2(1500, 1000)}) {
            made.emplace_back(xz.x, 0.0f, xz.y);
        }
        return made;
    }();
    const glm::vec2 drawable(1.42f, 0.92f);
    const double chosen = course_map_turn_degrees(points, drawable);
    CHECK(std::abs(chosen - 6.9) < 1.0e-9);
    const double scale = course_map_fit_scale(points, chosen, drawable);
    for (int degrees = -89; degrees <= 90; ++degrees) {
        CHECK(scale >= course_map_fit_scale(points, degrees, drawable));
    }
    // Equal scales go to the smaller turn: a square fits as well turned a quarter way round.
    CHECK(course_map_turn_degrees(box_outline(glm::vec3(0.0f), glm::vec3(100.0f, 0.0f, 100.0f)), drawable) == 0.0);
}

TEST_CASE("the course map turns a course lying corner to corner to the angle that fits it largest") {
    render_data data;
    // 1000 m long and 150 m wide, running north-east.
    const glm::vec3 along = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    const glm::vec3 across(-along.z, 0.0f, along.x);
    data.course_map_outline = {-along * 500.0f - across * 75.0f, along * 500.0f - across * 75.0f,
                               along * 500.0f + across * 75.0f, -along * 500.0f + across * 75.0f};
    for (const overlay_grid grid : {overlay_grid{554, 416}, overlay_grid{640, 360}, overlay_grid{733, 314}}) {
        const course_map_layout layout = make_course_map_layout(data, grid);
        const glm::vec2 drawable = course_map_drawable(grid);
        const double degrees = std::atan2(static_cast<double>(layout.turn.y), static_cast<double>(layout.turn.x)) * 180.0 / 3.14159265358979;
        CHECK(std::abs(degrees - course_map_turn_degrees(data.course_map_outline, drawable)) < 0.001);
        // At least as large as at any whole degree, and larger than unturned or with its length level.
        const double scale = layout.scale.y * pixels_per_clip(grid).y;
        for (int turn = -89; turn <= 90; ++turn) {
            CHECK(scale >= course_map_fit_scale(data.course_map_outline, turn, drawable) * 0.9999);
        }
        CHECK(scale > course_map_fit_scale(data.course_map_outline, 0.0, drawable));
        CHECK(scale > course_map_fit_scale(data.course_map_outline, -45.0, drawable));
    }
}

TEST_CASE("walking off the course map never moves it; an arrow at its edge points out to me") {
    render_data data;
    data.course_map_outline = box_outline(glm::vec3(0.0f), glm::vec3(400.0f, 0.0f, 400.0f));
    data.player_position = glm::vec3(200.0f, 0.0f, 200.0f);
    for (const overlay_grid grid : {overlay_grid{554, 416}, overlay_grid{640, 360}, overlay_grid{733, 314}}) {
        const course_map_layout on_course = make_course_map_layout(data, grid);
        for (const glm::vec3 away : {glm::vec3(1500.0f, 0.0f, 300.0f), glm::vec3(-600.0f, 0.0f, -900.0f),
                                     glm::vec3(100.0f, 0.0f, 700.0f)}) {
            render_data walked = data;
            walked.player_position = away;
            const course_map_layout layout = make_course_map_layout(walked, grid);
            CHECK((layout.center == on_course.center && layout.half_size == on_course.half_size &&
                   layout.world_center == on_course.world_center && layout.turn == on_course.turn && layout.scale == on_course.scale));
            overlay_batch batch;
            batch.grid = grid;
            draw_course_map_marks(batch, shipped_text_assets(), layout, walked, inks());
            // The arrow: the first triangles drawn (a quad with two corners at its tip), outline then
            // fill; the card's north arrow comes after.
            std::vector<std::size_t> triangles;
            for (std::size_t i = 0; i + overlay_vertices_per_quad <= batch.vertices.size(); i += overlay_vertices_per_quad) {
                if (batch.vertices[i + 1].position == batch.vertices[i + 2].position) {
                    triangles.push_back(i);
                }
            }
            REQUIRE(triangles.size() == 4U);
            const std::size_t fill = triangles[1];
            const glm::vec2 tip = batch.vertices[fill + 1].position;
            const glm::vec2 back = (batch.vertices[fill].position + batch.vertices[fill + 5].position) * 0.5f;
            CHECK(glm::all(glm::greaterThan(tip, layout.center - layout.half_size)));
            CHECK(glm::all(glm::lessThan(tip, layout.center + layout.half_size)));
            // Pointing at me, as the screen shows it.
            const glm::vec2 pixels(static_cast<float>(grid.width), static_cast<float>(grid.height));
            const glm::vec2 pointing = glm::normalize((tip - back) * pixels);
            const glm::vec2 to_me = glm::normalize((map_point(layout, away) - back) * pixels);
            CHECK(glm::dot(pointing, to_me) > 0.999f);
        }
    }
}

TEST_CASE("walking about the course map never moves a hole's number; I am drawn under them") {
    render_data data;
    data.course_map_outline = box_outline(glm::vec3(0.0f), glm::vec3(400.0f, 0.0f, 400.0f));
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 tee(static_cast<float>(i) * 100.0f, 0.0f, 0.0f);
        data.map_holes.push_back(render_map_hole{std::to_string(i + 1), {tee, tee + glm::vec3(0.0f, 0.0f, 300.0f)}});
    }
    const overlay_grid grid{640, 360};
    data.player_position = glm::vec3(300.0f, 0.0f, 380.0f);
    const course_map_layout away = make_course_map_layout(data, grid);
    const std::vector<ui_rect> before = course_map_number_boxes(away, grid, data);
    // Standing right on hole 2's number.
    data.player_position = glm::vec3(100.0f, 0.0f, 150.0f);
    const course_map_layout layout = make_course_map_layout(data, grid);
    const std::vector<ui_rect> after = course_map_number_boxes(layout, grid, data);
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        CHECK(glm::length(after[i].center - before[i].center) < 0.0001f);
    }
    CHECK(glm::length(after[1].center - map_point(layout, data.player_position)) < 0.0001f);
}

TEST_CASE("every hole's number sits in the middle of its hole on the course map, inside the paper and clear of the others") {
    render_data data;
    // Wider than long, so the map is not turned.
    data.course_map_outline = box_outline(glm::vec3(-100.0f, 0.0f, 0.0f), glm::vec3(700.0f, 0.0f, 750.0f));
    const auto straight = [](const glm::vec3 tee, const glm::vec3 pin) {
        return std::vector<glm::vec3>{tee, (tee + pin) * 0.5f, pin};
    };
    for (int i = 0; i < 18; ++i) {
        const glm::vec3 tee(static_cast<float>(i % 6) * 100.0f, 0.0f, static_cast<float>(i / 6) * 250.0f);
        data.map_holes.push_back(render_map_hole{std::to_string(i + 1), straight(tee, tee + glm::vec3(0.0f, 0.0f, 200.0f))});
    }
    // Played back down the first hole, as two holes sharing a fairway.
    data.map_holes.push_back(render_map_hole{"19", straight(glm::vec3(2.0f, 0.0f, 200.0f), glm::vec3(2.0f, 0.0f, 0.0f))});
    const text_assets& text = shipped_text_assets();
    for (const overlay_grid grid : {overlay_grid{554, 416}, overlay_grid{640, 360}, overlay_grid{733, 314}}) {
        const course_map_layout layout = make_course_map_layout(data, grid);
        const std::vector<ui_rect> boxes = course_map_number_boxes(layout, grid, data);
        REQUIRE(boxes.size() == data.map_holes.size());
        CHECK(glm::length(boxes[0].center - map_point(layout, glm::vec3(0.0f, 0.0f, 100.0f))) < 0.001f);
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const std::vector<glm::vec3>& line = data.map_holes[i].line_of_play;
            const glm::vec2 tee = map_point(layout, line.front());
            const glm::vec2 pin = map_point(layout, line.back());
            const glm::vec2 offset = boxes[i].center - tee;
            const glm::vec2 hole = pin - tee;
            CHECK(std::abs(offset.x * hole.y - offset.y * hole.x) / glm::length(hole) < 0.001f);
            const float along = glm::dot(offset, hole) / glm::dot(hole, hole);
            CHECK(along >= 0.25f);
            CHECK(along <= 0.75f);
            CHECK(rect_left(boxes[i]) >= layout.center.x - layout.half_size.x - 0.0001f);
            CHECK(rect_right(boxes[i]) <= layout.center.x + layout.half_size.x + 0.0001f);
            CHECK(rect_bottom(boxes[i]) >= layout.center.y - layout.half_size.y - 0.0001f);
            CHECK(rect_top(boxes[i]) <= layout.center.y + layout.half_size.y + 0.0001f);
            for (std::size_t j = 0; j < i; ++j) {
                const glm::vec2 gap = glm::abs(boxes[i].center - boxes[j].center) - (boxes[i].half_size + boxes[j].half_size);
                CHECK((gap.x >= 0.0f || gap.y >= 0.0f));
            }
        }
        overlay_batch batch;
        batch.grid = grid;
        draw_course_map_marks(batch, text, layout, data, inks());
        CHECK(batch.truncated_text_count == 0U);
    }
}

TEST_CASE("every hole's tee box is drawn on the course map at its tee, lying across its hole") {
    render_data data;
    data.course_map_outline = box_outline(glm::vec3(0.0f), glm::vec3(400.0f, 0.0f, 400.0f));
    const auto box_at = [](const glm::vec3& center, const glm::vec3& down_hole) {
        tee_box box;
        box.center = center;
        box.down_hole = down_hole;
        box.half_width = 4.8f;
        box.half_length = 3.2f;
        return box;
    };
    // Hole 1 plays up +z, hole 2 along +x.
    const glm::vec3 first(50.0f, 0.0f, 50.0f);
    const glm::vec3 second(150.0f, 0.0f, 350.0f);
    data.map_holes.push_back(render_map_hole{"1", {first, first + glm::vec3(0.0f, 0.0f, 250.0f)},
                                             box_at(first, glm::vec3(0.0f, 0.0f, 1.0f))});
    data.map_holes.push_back(render_map_hole{"2", {second, second + glm::vec3(220.0f, 0.0f, 0.0f)},
                                             box_at(second, glm::vec3(1.0f, 0.0f, 0.0f))});
    data.player_position = glm::vec3(200.0f, 0.0f, 200.0f);
    overlay_batch batch;
    batch.grid = overlay_grid{640, 480};
    const course_map_layout layout = make_course_map_layout(data, batch.grid);
    draw_course_map_marks(batch, shipped_text_assets(), layout, data, inks());
    // The tee boxes' fills: whole quads in the tee's ink, fully opaque.
    std::vector<std::pair<glm::vec2, glm::vec2>> boxes;  // centre, half extent
    for (std::size_t i = 0; i + overlay_vertices_per_quad <= batch.vertices.size(); i += overlay_vertices_per_quad) {
        const overlay_vertex& vertex = batch.vertices[i];
        if (glm::length(glm::vec3(vertex.color) - inks().tee) > 0.001f || vertex.color.a < 0.99f) {
            continue;
        }
        glm::vec2 low(1.0e9f), high(-1.0e9f);
        for (std::size_t k = 0; k < overlay_vertices_per_quad; ++k) {
            low = glm::min(low, batch.vertices[i + k].position);
            high = glm::max(high, batch.vertices[i + k].position);
        }
        boxes.emplace_back((low + high) * 0.5f, (high - low) * 0.5f);
    }
    REQUIRE(boxes.size() == 2U);
    for (std::size_t i = 0; i < 2; ++i) {
        const glm::vec2 tee = map_point(layout, data.map_holes[i].tee->center);
        CHECK(glm::length(boxes[i].first - tee) < 0.002f);
    }
    // Wider across its hole than along it: hole 1's across the screen's x,
    // hole 2's along its y, as the map turns the world.
    const glm::vec2 x_way = map_point(layout, glm::vec3(1.0f, 0.0f, 0.0f)) - map_point(layout, glm::vec3(0.0f));
    const bool world_x_is_screen_x = std::abs(x_way.x) > std::abs(x_way.y);
    CHECK((boxes[0].second.x > boxes[0].second.y) == world_x_is_screen_x);
    CHECK((boxes[1].second.y > boxes[1].second.x) == world_x_is_screen_x);
}
