#pragma once

// The paper course map's ground and everything printed on it, built once per
// course from the game's surfaces and nothing else: each texel of the paper
// is classified by surface_at (game/area_surface.h, the rule the ball plays
// by) at the ground under it and printed in that surface's flat ink, the
// rough included; the paper shows only where there is no ground. Cart paths,
// fences, trees (each a canopy disc of its real crown) and every hole's pin
// are kept as marks over it.
// The sheet never reads a render mesh, vertex colour or GPU buffer, and it
// never changes with the hole played. paint_course_map_sheet turns it into
// the image the paper shows, with overlay_raster.h like the hole signs'
// faces. The sheet covers the paper exactly, so it fits the paper at any
// window (course_map_layout.h). GL-free.

#include "game/lighting_loader.h"
#include "game/play_area.h"
#include "renderer/bmp_image.h"
#include "renderer/course_map_layout.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>

// A texel with no ground under it.
inline constexpr std::uint8_t sheet_no_ground = 255;

// A tree's crown seen from above, in texels from the sheet's bottom-left
// corner.
struct sheet_tree {
    glm::vec2 center{0.0f};
    float radius = 0.0f;  // the crown's, at least least_tree_radius
};

// The smallest crown printed, so a small or distant tree still reads.
inline constexpr float least_tree_radius = 1.25f;

// Marks over the ground, in texels from the sheet's bottom-left corner
// (x right, y up).
struct course_map_sheet {
    // The layout the sheet is fitted with, on its own grid: the paper's
    // shape and scale at any window.
    course_map_layout layout;
    int width = 0;  // texels across the paper
    int height = 0;
    // Per texel, row 0 at the bottom: its terrain_material or sheet_no_ground.
    std::vector<std::uint8_t> ground;
    std::vector<std::vector<glm::vec2>> cart_paths;
    std::vector<std::vector<glm::vec2>> fences;
    std::vector<sheet_tree> trees;  // every tree standing on ground
    std::vector<glm::ivec2> pins;   // every hole's pin
    // Changes with every sheet built, so a holder of its image knows to paint it again.
    std::uint64_t revision = 0;
    // World units along a texel's side.
    float texel_size = 0.0f;
};

// The sheet of `area`'s course, its texels one UI pixel of a picture
// `lines` UI pixels high (picture_style::ui_lines). Empty (no ground) for an
// area without holes.
course_map_sheet build_course_map_sheet(const play_area& area, int lines, std::uint64_t revision);

// The texel (column, row from the bottom) a world point is over; may lie off the sheet.
glm::ivec2 sheet_texel(const course_map_sheet& sheet, const glm::vec3& position);
// Whether a texel has ground under it.
bool sheet_on_ground(const course_map_sheet& sheet, glm::ivec2 texel);
// The terrain_material printed at a texel, none off the sheet or off the ground.
std::optional<terrain_material> sheet_material(const course_map_sheet& sheet, glm::ivec2 texel);

// The sheet as the paper shows it: opaque ink where there is ground, clear
// elsewhere. Trees are drawn as printed course guides draw them: a shadow
// offset to the lower right under each, then the crowns, then a one-texel
// outline round the mass they form.
rgba_image paint_course_map_sheet(const course_map_sheet& sheet, const map_inks& inks);
