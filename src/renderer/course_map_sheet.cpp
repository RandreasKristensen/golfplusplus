#include "renderer/course_map_sheet.h"

#include "game/area_surface.h"
#include "game/hole_sign.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/vector_relational.hpp>

namespace {
// The texel a world point is over, as a fractional position.
glm::vec2 texel_position(const course_map_sheet& sheet, const glm::vec3& position) {
    const glm::vec2 ndc = (map_point(sheet.layout, position) - sheet.layout.center) / sheet.layout.half_size;
    return (ndc * 0.5f + 0.5f) * glm::vec2(static_cast<float>(sheet.width), static_cast<float>(sheet.height));
}

void classify_ground(course_map_sheet& sheet, const play_area& area) {
    const course_map_layout& layout = sheet.layout;
    const glm::vec2 reach = layout.half_size - glm::vec2(course_map_inset);
    for (int row = 0; row < sheet.height; ++row) {
        for (int column = 0; column < sheet.width; ++column) {
            const glm::vec2 ndc(-1.0f + 2.0f * (static_cast<float>(column) + 0.5f) / static_cast<float>(sheet.width),
                                -1.0f + 2.0f * (static_cast<float>(row) + 0.5f) / static_cast<float>(sheet.height));
            if (glm::any(glm::greaterThan(glm::abs(ndc * layout.half_size), reach))) {
                continue;
            }
            const glm::vec3 point = map_world_point(layout, layout.center + ndc * layout.half_size);
            if (!ground_at(area, point)) {
                continue;
            }
            sheet.ground[static_cast<std::size_t>(row * sheet.width + column)] =
                static_cast<std::uint8_t>(surface_at(area, point).material);
        }
    }
}

std::vector<glm::vec2> texel_line(const course_map_sheet& sheet, const std::vector<glm::vec3>& points) {
    std::vector<glm::vec2> line;
    line.reserve(points.size());
    for (const glm::vec3& point : points) {
        line.push_back(texel_position(sheet, point));
    }
    return line;
}

void gather_marks(course_map_sheet& sheet, const play_area& area) {
    for (const cart_path& path : area.cart_paths) {
        sheet.cart_paths.push_back(texel_line(sheet, path.joints));
    }
    for (const course_world_fence& fence : area.fences) {
        sheet.fences.push_back(texel_line(sheet, fence.poles));
    }
    for (const tree_instance& tree : area.trees) {
        if (tree.shape.kind != tree_kind::tree || !sheet_on_ground(sheet, sheet_texel(sheet, tree.position))) {
            continue;
        }
        sheet.trees.push_back({texel_position(sheet, tree.position),
                               std::max(least_tree_radius, tree.shape.leaf_radius / sheet.texel_size)});
    }
    for (const hole_sign& sign : area.signs) {
        if (!sign.line_of_play.empty()) {
            sheet.pins.push_back(sheet_texel(sheet, sign.line_of_play.back()));
        }
    }
}
}

course_map_sheet build_course_map_sheet(const play_area& area, const int lines, const std::uint64_t revision) {
    course_map_sheet sheet;
    sheet.revision = revision;
    sheet.height = std::max(1, static_cast<int>(std::lround(2.0f * course_map_paper_half_height * static_cast<float>(lines) * 0.5f)));
    sheet.width = std::max(1, static_cast<int>(std::lround(static_cast<float>(sheet.height) * course_map_paper_aspect)));
    // A wide picture, so the paper is not narrowed (course_map_layout.cpp).
    const overlay_grid grid{(lines * 16 + 8) / 9, lines};
    sheet.layout = make_course_map_layout(area.holes_outline, area.center, grid);
    sheet.texel_size = 1.0f / (sheet.layout.scale.y * pixels_per_clip(grid).y);
    sheet.ground.assign(static_cast<std::size_t>(sheet.width * sheet.height), sheet_no_ground);
    if (area.holes_outline.size() < 3 || area.holes.empty()) {
        return sheet;
    }
    classify_ground(sheet, area);
    gather_marks(sheet, area);
    return sheet;
}

glm::ivec2 sheet_texel(const course_map_sheet& sheet, const glm::vec3& position) {
    const glm::vec2 at = texel_position(sheet, position);
    return glm::ivec2(static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)));
}

bool sheet_on_ground(const course_map_sheet& sheet, const glm::ivec2 texel) {
    return texel.x >= 0 && texel.y >= 0 && texel.x < sheet.width && texel.y < sheet.height &&
           sheet.ground[static_cast<std::size_t>(texel.y * sheet.width + texel.x)] != sheet_no_ground;
}

std::optional<terrain_material> sheet_material(const course_map_sheet& sheet, const glm::ivec2 texel) {
    if (!sheet_on_ground(sheet, texel)) {
        return std::nullopt;
    }
    return static_cast<terrain_material>(sheet.ground[static_cast<std::size_t>(texel.y * sheet.width + texel.x)]);
}
