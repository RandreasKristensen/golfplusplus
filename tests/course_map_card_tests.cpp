#include "doctest.h"

#include "game/text_ids.h"
#include "renderer/course_map_card.h"
#include "renderer/course_map_layout.h"
#include "renderer/course_map_overlay.h"
#include "renderer/map_ink.h"
#include "renderer/overlay_batch.h"

#include "test_support.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
const map_inks& inks() {
    return test_scene_colors().map;
}

std::vector<glm::vec3> box_outline(const glm::vec3& low, const glm::vec3& high) {
    return {low, glm::vec3(high.x, 0.0f, low.z), high, glm::vec3(low.x, 0.0f, high.z)};
}

const std::array<overlay_grid, 3> windows{overlay_grid{554, 416}, overlay_grid{640, 360}, overlay_grid{733, 314}};
}

TEST_CASE("the shipped map inks tell every surface apart, on the paper and faded on a hole sign") {
    const auto palette = load_lighting(asset_root());
    REQUIRE(palette.has_value());
    const map_inks& shipped = palette->colors.map;
    const std::vector<terrain_material> surfaces{terrain_material::fairway, terrain_material::rough, terrain_material::green,
                                                 terrain_material::bunker, terrain_material::water};
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        const glm::vec3 ink = map_material_ink(shipped, surfaces[i]);
        CHECK(glm::length(ink - shipped.paper) > 0.15f);
        // Another hole's on a hole sign, faded: still not the paper, and not the hole shown's.
        const glm::vec3 faded = map_hole_ink(shipped, surfaces[i], false);
        if (surfaces[i] != terrain_material::bunker && surfaces[i] != terrain_material::water) {
            CHECK(glm::length(faded - shipped.paper) > 0.04f);
            CHECK(glm::length(faded - ink) > 0.1f);
        }
        for (std::size_t j = i + 1; j < surfaces.size(); ++j) {
            CHECK(glm::length(ink - map_material_ink(shipped, surfaces[j])) > 0.08f);
        }
        // The marks over the ground read on every surface.
        CHECK(glm::length(shipped.cart_path - ink) > 0.2f);
        CHECK(glm::length(shipped.flag - ink) > 0.2f);
    }
    CHECK(glm::length(shipped.tee - shipped.paper) > 0.15f);
}

TEST_CASE("another hole's ground fades towards the paper; bunkers and water are as they are") {
    const map_inks& map = inks();
    for (const terrain_material surface : {terrain_material::fairway, terrain_material::rough, terrain_material::green}) {
        const glm::vec3 full = map_material_ink(map, surface);
        CHECK(map_hole_ink(map, surface, true) == full);
        CHECK(glm::length(map_hole_ink(map, surface, false) - glm::mix(full, map.paper, map.other_hole_fade)) < 0.0001f);
    }
    for (const terrain_material hazard : {terrain_material::bunker, terrain_material::water}) {
        CHECK(map_hole_ink(map, hazard, false) == map_material_ink(map, hazard));
    }
}

TEST_CASE("the paper maps the world the same way at every window, so a sheet fits it at any window") {
    render_data data;
    data.course_map_outline = box_outline(glm::vec3(-100.0f, 0.0f, 50.0f), glm::vec3(900.0f, 0.0f, 450.0f));
    const glm::vec3 point(120.0f, 0.0f, 300.0f);
    glm::vec2 first(0.0f);
    bool seen = false;
    for (const overlay_grid& grid : windows) {
        const course_map_layout layout = make_course_map_layout(data, grid);
        const glm::vec2 on_paper = (map_point(layout, point) - layout.center) / layout.half_size;
        if (seen) {
            CHECK(glm::length(on_paper - first) < 0.001f);
        }
        first = on_paper;
        seen = true;
        // And back to the world.
        CHECK(glm::length(map_world_point(layout, map_point(layout, point)) - point) < 0.01f);
    }
}

TEST_CASE("north points true on the turned paper: down unturned, right turned a quarter, up turned half") {
    course_map_layout layout;
    // Unturned the paper has +Z up, so north (-Z) is down.
    CHECK(glm::length(course_map_north(layout) - glm::vec2(0.0f, -1.0f)) < 0.0001f);
    layout.turn = glm::vec2(0.0f, 1.0f);
    CHECK(glm::length(course_map_north(layout) - glm::vec2(1.0f, 0.0f)) < 0.0001f);
    layout.turn = glm::vec2(-1.0f, 0.0f);
    CHECK(glm::length(course_map_north(layout) - glm::vec2(0.0f, 1.0f)) < 0.0001f);
}

TEST_CASE("the north arrow on the card points where north is on screen, for a course turned any way") {
    const text_assets& text = shipped_text_assets();
    for (const float degrees : {0.0f, 30.0f, 90.0f, 137.0f, 200.0f, -60.0f}) {
        // A course lying `degrees` off the paper's across.
        const float radians = glm::radians(degrees);
        const glm::vec3 along(std::cos(radians), 0.0f, std::sin(radians));
        const glm::vec3 across(-along.z, 0.0f, along.x);
        render_data data;
        data.course_map_outline = {-along * 500.0f - across * 60.0f, along * 500.0f - across * 60.0f,
                                   along * 500.0f + across * 60.0f, -along * 500.0f + across * 60.0f};
        overlay_batch batch;
        batch.grid = overlay_grid{640, 360};
        const course_map_layout layout = make_course_map_layout(data, batch.grid);
        const glm::vec2 pixels(320.0f, 180.0f);
        // Where north lies on screen, from the world itself.
        const glm::vec3 here = layout.world_center;
        const glm::vec2 north =
            glm::normalize((map_point(layout, here + glm::vec3(0.0f, 0.0f, -100.0f)) - map_point(layout, here)) * pixels);
        CHECK(glm::length(course_map_north(layout) - north) < 0.001f);

        draw_course_map_card(batch, text, layout, data, inks());
        // The arrow: the triangle in the flag's ink (a quad with two corners at its tip).
        std::size_t found = 0;
        for (std::size_t i = 0; i + overlay_vertices_per_quad <= batch.vertices.size(); i += overlay_vertices_per_quad) {
            if (batch.vertices[i + 1].position != batch.vertices[i + 2].position ||
                glm::length(glm::vec3(batch.vertices[i].color) - inks().flag) > 0.001f) {
                continue;
            }
            ++found;
            const glm::vec2 tip = batch.vertices[i + 1].position;
            const glm::vec2 back = (batch.vertices[i].position + batch.vertices[i + 5].position) * 0.5f;
            CHECK(glm::dot(glm::normalize((tip - back) * pixels), north) > 0.999f);
        }
        CHECK(found == 1U);
        CHECK(batch.truncated_text_count == 0U);
    }
}

TEST_CASE("the scale bar is the longest round length that is at most a quarter of the paper") {
    render_data data;
    data.course_map_outline = box_outline(glm::vec3(0.0f), glm::vec3(1000.0f, 0.0f, 400.0f));
    const overlay_grid grid{640, 360};
    const course_map_layout layout = make_course_map_layout(data, grid);
    const float paper_pixels = layout.half_size.x * 2.0f * 320.0f;
    const float pixels_per_unit = layout.scale.y * 180.0f;
    for (const float meters_per_unit : {1.0f, 0.5f, 2.0f}) {
        const int meters = course_map_scale_meters(layout, grid, meters_per_unit);
        const int next = meters % 5 == 0 ? meters * 2 : meters / 2 * 5;
        CHECK(static_cast<float>(meters) / meters_per_unit * pixels_per_unit <= paper_pixels * 0.25f);
        CHECK(static_cast<float>(next) / meters_per_unit * pixels_per_unit > paper_pixels * 0.25f);
    }
    // A 1000 m course across 410 pixels is about 0.41 px a metre: 200 m is 82 px.
    CHECK(course_map_scale_meters(layout, grid, 1.0f) == 200);
}

TEST_CASE("the card's scale bar and north arrow fit inside the paper and clear of each other at every window") {
    const text_assets& text = shipped_text_assets();
    for (const overlay_grid& grid : windows) {
        render_data data;
        data.course_map_outline = box_outline(glm::vec3(0.0f), glm::vec3(900.0f, 0.0f, 500.0f));
        const course_map_layout layout = make_course_map_layout(data, grid);
        overlay_batch batch;
        batch.grid = grid;
        draw_course_map_card(batch, text, layout, data, inks());
        CHECK(batch.truncated_text_count == 0U);
        const std::vector<ui_rect> chips = course_map_card_boxes(layout, grid, data);
        REQUIRE(chips.size() == 2U);
        for (std::size_t i = 0; i < chips.size(); ++i) {
            CHECK(rect_left(chips[i]) >= layout.center.x - layout.half_size.x);
            CHECK(rect_right(chips[i]) <= layout.center.x + layout.half_size.x);
            CHECK(rect_bottom(chips[i]) >= layout.center.y - layout.half_size.y);
            CHECK(rect_top(chips[i]) <= layout.center.y + layout.half_size.y);
            for (std::size_t j = 0; j < i; ++j) {
                const glm::vec2 gap = glm::abs(chips[i].center - chips[j].center) - (chips[i].half_size + chips[j].half_size);
                CHECK((gap.x >= 0.0f || gap.y >= 0.0f));
            }
        }
    }
}
