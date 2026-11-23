#include "doctest.h"

#include "core/render_frame.h"
#include "game/area_surface.h"
#include "game/course_session.h"
#include "game/game_state.h"
#include "renderer/course_map_overlay.h"
#include "renderer/course_map_sheet.h"
#include "renderer/map_ink.h"
#include "renderer/map_texels.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>

namespace {
constexpr int sheet_lines = 360;

const map_inks& inks() {
    return test_scene_colors().map;
}

course_map_sheet sheet_of(const play_area& area) {
    return build_course_map_sheet(area, sheet_lines, 1);
}

glm::vec3 pixel(const rgba_image& image, const glm::ivec2 texel) {
    const std::size_t at = static_cast<std::size_t>(texel.y * image.width + texel.x) * 4;
    return glm::vec3(image.pixels[at], image.pixels[at + 1], image.pixels[at + 2]) / 255.0f;
}

bool same_ink(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(a - b) < 0.005f;
}

// The sheet's texel area in world units squared.
float texel_area(const course_map_sheet& sheet) {
    return sheet.texel_size * sheet.texel_size;
}

// The world point at the middle of every texel of `material`.
std::vector<glm::vec3> texel_points(const course_map_sheet& sheet, const terrain_material material) {
    std::vector<glm::vec3> points;
    for (int row = 0; row < sheet.height; ++row) {
        for (int column = 0; column < sheet.width; ++column) {
            if (sheet_material(sheet, glm::ivec2(column, row)) == material) {
                const glm::vec2 clip = sheet.layout.center +
                    glm::vec2(-1.0f + 2.0f * (static_cast<float>(column) + 0.5f) / static_cast<float>(sheet.width),
                              -1.0f + 2.0f * (static_cast<float>(row) + 0.5f) / static_cast<float>(sheet.height)) *
                        sheet.layout.half_size;
                points.push_back(map_world_point(sheet.layout, clip));
            }
        }
    }
    return points;
}

// How many of `points` lie in a zone's box.
std::size_t points_in_box(const std::vector<glm::vec3>& points, const material_zone& zone) {
    return static_cast<std::size_t>(std::count_if(points.begin(), points.end(), [&](const glm::vec3& point) {
        return std::abs(point.x - zone.center.x) <= zone.half_size.x && std::abs(point.z - zone.center.z) <= zone.half_size.y;
    }));
}

// The pin of every hole is on green (within a texel) and the tee of every hole with a tee
// box is under that box, in the tee's ink.
void check_pins_and_tees(const game_state& state, const course_map_sheet& sheet) {
    const text_assets& text = shipped_text_assets();
    REQUIRE(!state.area.signs.empty());

    game_state shown = state;
    shown.course_map_active = true;
    render_meshes meshes;
    meshes.course_map = &sheet;
    const render_data data = make_render_data(shown, text, shipped_content().skills, meshes, test_props(), 60.0f, false);
    const overlay_grid grid{640, 360};
    const course_map_layout layout = make_course_map_layout(data, grid);
    overlay_batch batch;
    batch.grid = grid;
    draw_course_map_marks(batch, text, layout, data, inks());
    CHECK(batch.truncated_text_count == 0U);

    for (const hole_sign& sign : state.area.signs) {
        const glm::vec3 pin = sign.line_of_play.back();
        const glm::ivec2 pin_texel = sheet_texel(sheet, pin);
        // A small green may fall between texel middles when a pin is by its edge: it is on green within a texel.
        bool on_green = false;
        for (int across = -1; across <= 1; ++across) {
            for (int up = -1; up <= 1; ++up) {
                on_green = on_green || sheet_material(sheet, pin_texel + glm::ivec2(across, up)) == terrain_material::green;
            }
        }
        CHECK(on_green);

        if (hole_tee_box(state.area.tee_boxes, sign.area_hole) == nullptr) {
            continue;
        }
        const glm::vec2 tee = map_point(layout, sign.line_of_play.front());
        bool on_tee_ink = false;
        for (std::size_t i = 0; i + overlay_vertices_per_quad <= batch.vertices.size(); i += overlay_vertices_per_quad) {
            if (!same_ink(glm::vec3(batch.vertices[i].color), inks().tee) || batch.vertices[i].color.a < 0.99f) {
                continue;
            }
            glm::vec2 low(1.0e9f);
            glm::vec2 high(-1.0e9f);
            for (std::size_t k = 0; k < overlay_vertices_per_quad; ++k) {
                low = glm::min(low, batch.vertices[i + k].position);
                high = glm::max(high, batch.vertices[i + k].position);
            }
            on_tee_ink = on_tee_ink || (glm::all(glm::greaterThanEqual(tee, low)) && glm::all(glm::lessThanEqual(tee, high)));
        }
        CHECK(on_tee_ink);
    }
}

// Each hole's green and its bunkers are printed in their own inks, and a course with fairway prints it.
void check_hole_inks(const game_state& state, const course_map_sheet& sheet) {
    const std::vector<glm::vec3> green_points = texel_points(sheet, terrain_material::green);
    const std::vector<glm::vec3> bunker_points = texel_points(sheet, terrain_material::bunker);
    const play_area& area = state.area;
    // A feature smaller than a couple of texels may fall between them.
    const float resolvable = 2.0f * texel_area(sheet);
    bool fairway = false;
    for (std::size_t hole = 0; hole < area.holes.size(); ++hole) {
        float green_area = 0.0f;
        float bunker_area = 0.0f;
        std::size_t greens = 0;
        std::size_t bunkers = 0;
        for (std::size_t i = 0; i < area.zones.size(); ++i) {
            if (i >= area.zone_holes.size() || area.zone_holes[i] != hole) {
                continue;
            }
            const material_zone& zone = area.zones[i];
            if (zone.type == material_zone_type::green) {
                green_area += zone_area(zone);
                greens += points_in_box(green_points, zone);
            }
            if (zone.type == material_zone_type::bunker) {
                bunker_area += zone_area(zone);
                bunkers += points_in_box(bunker_points, zone);
            }
            fairway = fairway || zone.type == material_zone_type::fairway;
        }
        fairway = fairway || std::any_of(area.holes[hole].vertices.begin(), area.holes[hole].vertices.end(),
                                         [](const terrain_vertex& vertex) { return vertex.material == terrain_material::fairway; });
        if (green_area > resolvable) {
            CHECK(greens > 0U);
        }
        if (bunker_area > resolvable) {
            CHECK(bunkers > 0U);
        }
    }
    if (fairway) {
        CHECK(!texel_points(sheet, terrain_material::fairway).empty());
    }
}

// Texel centres sample each green's area: the count's error is the texels along the edges, a few
// percent of a whole course's greens, and a bunker cut out of a green or two greens overlapping only
// takes some of it, so the printed area is within a quarter of the zones' either way.
void check_green_area(const course_definition& course, const game_state& state, const course_map_sheet& sheet) {
    constexpr float tolerance = 0.25f;
    float zones_area = 0.0f;
    for (const material_zone& zone : state.area.zones) {
        zones_area += zone.type == material_zone_type::green ? zone_area(zone) : 0.0f;
    }
    std::size_t texels = 0;
    for (const std::uint8_t ground : sheet.ground) {
        texels += ground == static_cast<std::uint8_t>(terrain_material::green) ? 1U : 0U;
    }
    const float printed = static_cast<float>(texels) * texel_area(sheet);
    REQUIRE(zones_area > 0.0f);
    const float ratio = printed / zones_area;
    if (std::abs(ratio - 1.0f) >= tolerance * 0.5f) {
        std::fprintf(stderr, "%s: printed %.0f of %.0f (%.2f)\n", course.id.c_str(), printed, zones_area, ratio);
    }
    CHECK(std::abs(ratio - 1.0f) < tolerance);
}

// Ground under every texel inside the paper's inset, and none outside it; trees stand on ground.
void check_paper_ground(const course_map_sheet& sheet) {
    REQUIRE(sheet.width > 0);
    const glm::vec2 reach = sheet.layout.half_size - glm::vec2(course_map_inset);
    std::size_t missing = 0;
    std::size_t stray = 0;
    for (int row = 0; row < sheet.height; ++row) {
        for (int column = 0; column < sheet.width; ++column) {
            const glm::vec2 ndc(-1.0f + 2.0f * (static_cast<float>(column) + 0.5f) / static_cast<float>(sheet.width),
                                -1.0f + 2.0f * (static_cast<float>(row) + 0.5f) / static_cast<float>(sheet.height));
            const bool inside = glm::all(glm::lessThanEqual(glm::abs(ndc * sheet.layout.half_size), reach));
            const bool ground = sheet_on_ground(sheet, glm::ivec2(column, row));
            missing += inside && !ground ? 1U : 0U;
            stray += !inside && ground ? 1U : 0U;
        }
    }
    CHECK(missing == 0U);
    CHECK(stray == 0U);
    for (const sheet_tree& tree : sheet.trees) {
        CHECK(sheet_on_ground(sheet, glm::ivec2(glm::floor(tree.center))));
    }
}
}

TEST_CASE("every shipped course's printed map has its pins on green, tees on tee ink, each hole's inks, its greens' area and ground to the paper's edges") {
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        const course_map_sheet sheet = sheet_of(state.area);
        check_pins_and_tees(state, sheet);
        check_hole_inks(state, sheet);
        check_green_area(course, state, sheet);
        check_paper_ground(sheet);
    }
}

TEST_CASE("the printed map prints the surface the game plays under every texel of ground: the rough where no zone or hole lies, paper only without ground") {
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        const course_map_sheet sheet = sheet_of(state.area);
        const rgba_image painted = paint_course_map_sheet(sheet, inks());
        std::size_t rough = 0;
        std::size_t zones_and_holes = 0;
        for (int row = 0; row < sheet.height; row += 2) {
            for (int column = 0; column < sheet.width; column += 2) {
                const glm::ivec2 texel(column, row);
                const std::size_t alpha = static_cast<std::size_t>(row * painted.width + column) * 4 + 3;
                if (!sheet_on_ground(sheet, texel)) {
                    CHECK(painted.pixels[alpha] == 0);
                    CHECK(!sheet_material(sheet, texel));
                    continue;
                }
                CHECK(painted.pixels[alpha] == 255);
                const glm::vec2 clip = sheet.layout.center +
                    glm::vec2(-1.0f + 2.0f * (static_cast<float>(column) + 0.5f) / static_cast<float>(sheet.width),
                              -1.0f + 2.0f * (static_cast<float>(row) + 0.5f) / static_cast<float>(sheet.height)) *
                        sheet.layout.half_size;
                const area_surface surface = surface_at(state.area, map_world_point(sheet.layout, clip));
                CHECK(sheet_material(sheet, texel) == surface.material);
                if (!surface.zone && !surface.on_hole) {
                    CHECK(surface.material == terrain_material::rough);
                    CHECK(sheet.ground[static_cast<std::size_t>(row * sheet.width + column)] ==
                          static_cast<std::uint8_t>(terrain_material::rough));
                    ++rough;
                } else {
                    ++zones_and_holes;
                }
            }
        }
        CHECK(rough > 100U);
        CHECK(zones_and_holes > 20U);
    }
}

// A sheet of rough with `trees` over it, nothing else.
rgba_image painted_trees(const std::vector<sheet_tree>& trees) {
    course_map_sheet sheet;
    sheet.width = 60;
    sheet.height = 40;
    sheet.ground.assign(static_cast<std::size_t>(sheet.width * sheet.height), static_cast<std::uint8_t>(terrain_material::rough));
    sheet.trees = trees;
    return paint_course_map_sheet(sheet, inks());
}

TEST_CASE("the printed map draws a tree as a canopy disc of its crown's size, shadowed to the lower right and outlined") {
    const rgba_image alone = painted_trees({{glm::vec2(20.0f, 20.0f), 4.0f}});
    CHECK(same_ink(pixel(alone, glm::ivec2(19, 19)), inks().tree));
    CHECK(same_ink(pixel(alone, glm::ivec2(23, 19)), inks().tree));  // a crown's radius out
    CHECK(same_ink(pixel(alone, glm::ivec2(19, 16)), inks().tree));
    CHECK(same_ink(pixel(alone, glm::ivec2(26, 19)), inks().rough));  // well clear
    // One outline texel round the crown, on every side.
    CHECK(same_ink(pixel(alone, glm::ivec2(24, 19)), inks().tree_outline));
    CHECK(same_ink(pixel(alone, glm::ivec2(15, 19)), inks().tree_outline));
    CHECK(same_ink(pixel(alone, glm::ivec2(19, 24)), inks().tree_outline));
    CHECK(same_ink(pixel(alone, glm::ivec2(19, 15)), inks().tree_outline));
    // The shadow lies past the outline on the lower right only.
    CHECK(same_ink(pixel(alone, glm::ivec2(22, 15)), inks().tree_shadow));
    CHECK(same_ink(pixel(alone, glm::ivec2(24, 17)), inks().tree_shadow));
    CHECK(same_ink(pixel(alone, glm::ivec2(15, 25)), inks().rough));
    CHECK(same_ink(pixel(alone, glm::ivec2(16, 24)), inks().rough));

    // A small crown is drawn at least least_tree_radius wide.
    const rgba_image small = painted_trees({{glm::vec2(30.0f, 20.0f), least_tree_radius}});
    CHECK(same_ink(pixel(small, glm::ivec2(29, 19)), inks().tree));
    CHECK(same_ink(pixel(small, glm::ivec2(30, 20)), inks().tree));

    // Two crowns overlapping make one wood: canopy between them, outline only round the whole.
    const rgba_image wood = painted_trees({{glm::vec2(20.0f, 20.0f), 4.0f}, {glm::vec2(26.0f, 20.0f), 4.0f}});
    CHECK(same_ink(pixel(wood, glm::ivec2(23, 19)), inks().tree));
    CHECK(same_ink(pixel(wood, glm::ivec2(23, 22)), inks().tree));
    CHECK(same_ink(pixel(wood, glm::ivec2(23, 23)), inks().tree_outline));
    CHECK(same_ink(pixel(wood, glm::ivec2(30, 19)), inks().tree_outline));
}

TEST_CASE("every shipped course's map has a crown for each tree on its ground, sized from the tree's real crown at the map's scale") {
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        const course_map_sheet sheet = sheet_of(state.area);
        std::size_t next = 0;
        std::size_t bigger_than_least = 0;
        for (const tree_instance& tree : state.area.trees) {
            if (tree.shape.kind != tree_kind::tree || !sheet_on_ground(sheet, sheet_texel(sheet, tree.position))) {
                continue;
            }
            REQUIRE(next < sheet.trees.size());
            const sheet_tree& crown = sheet.trees[next++];
            CHECK(std::abs(crown.radius - std::max(least_tree_radius, tree.shape.leaf_radius / sheet.texel_size)) < 0.0001f);
            bigger_than_least += crown.radius > least_tree_radius ? 1U : 0U;
        }
        CHECK(next == sheet.trees.size());
        CHECK(next > 50U);
        CHECK(bigger_than_least > 0U);
    }
}

TEST_CASE("the printed map is the same whatever hole is being played, and no line of play is drawn on it") {
    const course_definition* course = nullptr;
    for (const course_definition& candidate : shipped_content().courses) {
        course = course == nullptr || candidate.holes.size() > course->holes.size() ? &candidate : course;
    }
    REQUIRE(course != nullptr);
    REQUIRE(course->holes.size() >= 2U);
    const text_assets& text = shipped_text_assets();
    const overlay_grid grid{640, 360};
    // Everything the paper shows but the ball: the sheet's image and the marks over it.
    struct picture {
        rgba_image ground;
        std::vector<overlay_vertex> marks;
    };
    const auto picture_of = [&](const std::optional<std::size_t> hole) {
        game_state state = make_game_state(shipped_content(), save_data{});
        CHECK(start_course(state, *course));
        if (hole) {
            CHECK(tee_up(state, *hole, false));
        }
        const course_map_sheet sheet = sheet_of(state.area);
        state.course_map_active = true;
        render_meshes meshes;
        meshes.course_map = &sheet;
        render_data data = make_render_data(state, text, shipped_content().skills, meshes, test_props(), 60.0f, false);
        data.show_hole = false;
        data.player_position = state.area.signs.front().line_of_play.front();
        const course_map_layout layout = make_course_map_layout(data, grid);
        overlay_batch batch;
        batch.grid = grid;
        draw_course_map_marks(batch, text, layout, data, inks());
        return picture{paint_course_map_sheet(sheet, inks()), batch.vertices};
    };
    const picture none = picture_of(std::nullopt);
    for (const std::size_t hole : {std::size_t(0), std::size_t(1), course->holes.size() - 1}) {
        const picture other = picture_of(hole);
        CHECK(other.ground.pixels == none.ground.pixels);
        REQUIRE(other.marks.size() == none.marks.size());
        bool same = true;
        for (std::size_t i = 0; i < none.marks.size(); ++i) {
            same = same && other.marks[i].position == none.marks[i].position && other.marks[i].color == none.marks[i].color;
        }
        CHECK(same);
    }
    // Only the map's ground, tree, cart path, fence and flag inks are on the ground's image.
    std::size_t stray = 0;
    for (std::size_t i = 0; i < none.ground.pixels.size(); i += 4) {
        if (none.ground.pixels[i + 3] == 0) {
            continue;
        }
        const glm::vec3 ink = glm::vec3(none.ground.pixels[i], none.ground.pixels[i + 1], none.ground.pixels[i + 2]) / 255.0f;
        bool known = false;
        for (const glm::vec3& allowed : {inks().fairway, inks().rough, inks().heavy_rough, inks().green, inks().bunker,
                                         inks().water, inks().tree, inks().tree_shadow, inks().tree_outline,
                                         inks().cart_path, inks().fence, inks().flagstick, inks().flag}) {
            known = known || same_ink(ink, allowed);
        }
        stray += known ? 0U : 1U;
    }
    CHECK(stray == 0U);
}

TEST_CASE("every shipped course's map draws a flag at every hole's pin") {
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        const course_map_sheet sheet = sheet_of(state.area);
        const rgba_image painted = paint_course_map_sheet(sheet, inks());
        std::size_t holes = 0;
        for (const hole_sign& sign : state.area.signs) {
            if (sign.line_of_play.empty()) {
                continue;
            }
            ++holes;
            const glm::ivec2 pin = sheet_texel(sheet, sign.line_of_play.back());
            CHECK(std::count(sheet.pins.begin(), sheet.pins.end(), pin) == 1);
            // The stick up from the pin and the cloth at its top, to the right.
            for (int up = 0; up < 4; ++up) {
                const glm::vec3 there = pixel(painted, pin + glm::ivec2(0, up));
                CHECK((same_ink(there, inks().flagstick) || same_ink(there, inks().flag)));
            }
            CHECK(same_ink(pixel(painted, pin + glm::ivec2(1, 3)), inks().flag));
        }
        REQUIRE(holes > 0U);
        CHECK(sheet.pins.size() == holes);
    }
}
