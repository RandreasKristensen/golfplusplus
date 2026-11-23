#include "physics/surface_index.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/vec2.hpp>

namespace {
// Cells along the longer side at most.
constexpr float most_cells_a_side = 64.0f;

struct flat_box {
    glm::vec2 low{0.0f};
    glm::vec2 high{0.0f};
};

// A hole's vertices' box, padded as terrain.cpp pads a triangle's box: the
// barycentric tolerance lets a point just outside count as inside.
flat_box hole_box(const terrain_mesh& hole) {
    flat_box box{glm::vec2(std::numeric_limits<float>::max()), glm::vec2(std::numeric_limits<float>::lowest())};
    for (const terrain_vertex& vertex : hole.vertices) {
        box.low = glm::min(box.low, glm::vec2(vertex.position.x, vertex.position.z));
        box.high = glm::max(box.high, glm::vec2(vertex.position.x, vertex.position.z));
    }
    if (hole.vertices.empty()) {
        return box;
    }
    const float pad = 0.001f + 0.001f * std::max(box.high.x - box.low.x, box.high.y - box.low.y);
    box.low -= glm::vec2(pad);
    box.high += glm::vec2(pad);
    return box;
}

// Padded past rounding in the box test (material_zone.cpp in_box).
flat_box zone_box(const material_zone& zone) {
    constexpr float pad = 0.01f;
    return flat_box{glm::vec2(zone.center.x - zone.half_size.x - pad, zone.center.z - zone.half_size.y - pad),
                    glm::vec2(zone.center.x + zone.half_size.x + pad, zone.center.z + zone.half_size.y + pad)};
}

// The cell holding a coordinate, clamped to the grid: a box's ends and a
// point go through the same function, so a box holding a point always lists
// the point's cell.
int cell_along(const float value, const float origin, const float size, const int count) {
    const float cell = std::floor((value - origin) / size);
    return cell <= 0.0f ? 0 : (cell >= static_cast<float>(count - 1) ? count - 1 : static_cast<int>(cell));
}

// Items by cell, CSR style, ascending within a cell.
void fill_cells(const surface_index& grid, const std::vector<flat_box>& boxes, std::vector<std::uint32_t>& starts,
                std::vector<std::uint32_t>& items) {
    const std::size_t cells = static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows);
    starts.assign(cells + 1U, 0U);
    const auto each_cell = [&](const flat_box& box, const auto& visit) {
        for (int row = cell_along(box.low.y, grid.min_z, grid.cell_size, grid.rows);
             row <= cell_along(box.high.y, grid.min_z, grid.cell_size, grid.rows); ++row) {
            for (int column = cell_along(box.low.x, grid.min_x, grid.cell_size, grid.columns);
                 column <= cell_along(box.high.x, grid.min_x, grid.cell_size, grid.columns); ++column) {
                visit(static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) + static_cast<std::size_t>(column));
            }
        }
    };
    for (const flat_box& box : boxes) {
        each_cell(box, [&](const std::size_t cell) { ++starts[cell + 1U]; });
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
        starts[cell + 1U] += starts[cell];
    }
    items.assign(starts.back(), 0U);
    std::vector<std::uint32_t> next(starts.begin(), starts.end() - 1);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        each_cell(boxes[i], [&](const std::size_t cell) { items[next[cell]++] = static_cast<std::uint32_t>(i); });
    }
}

index_range cell_items(const surface_index& index, const std::vector<std::uint32_t>& starts,
                       const std::vector<std::uint32_t>& items, const glm::vec3& position) {
    const std::size_t cell = static_cast<std::size_t>(cell_along(position.z, index.min_z, index.cell_size, index.rows)) *
            static_cast<std::size_t>(index.columns) +
        static_cast<std::size_t>(cell_along(position.x, index.min_x, index.cell_size, index.columns));
    return index_range{items.data() + starts[cell], items.data() + starts[cell + 1U]};
}
}

surface_index build_surface_index(const std::vector<terrain_mesh>& holes, const std::vector<material_zone>& zones) {
    surface_index index;
    index.hole_count = holes.size();
    index.zone_count = zones.size();
    std::vector<flat_box> hole_boxes;
    std::vector<flat_box> zone_boxes;
    glm::vec2 low(std::numeric_limits<float>::max());
    glm::vec2 high(std::numeric_limits<float>::lowest());
    for (const terrain_mesh& hole : holes) {
        hole_boxes.push_back(hole_box(hole));
    }
    for (const material_zone& zone : zones) {
        zone_boxes.push_back(zone_box(zone));
    }
    for (const std::vector<flat_box>* boxes : {&hole_boxes, &zone_boxes}) {
        for (const flat_box& box : *boxes) {
            if (box.low.x <= box.high.x && box.low.y <= box.high.y) {
                low = glm::min(low, box.low);
                high = glm::max(high, box.high);
            }
        }
    }
    if (!(low.x <= high.x) || !(low.y <= high.y)) {
        return index;
    }
    index.min_x = low.x;
    index.min_z = low.y;
    index.cell_size = std::max({1.0f, (high.x - low.x) / most_cells_a_side, (high.y - low.y) / most_cells_a_side});
    index.columns = static_cast<int>(std::floor((high.x - low.x) / index.cell_size)) + 1;
    index.rows = static_cast<int>(std::floor((high.y - low.y) / index.cell_size)) + 1;
    fill_cells(index, hole_boxes, index.hole_starts, index.hole_items);
    fill_cells(index, zone_boxes, index.zone_starts, index.zone_items);
    return index;
}

std::optional<terrain_sample> sample_holes(const std::vector<terrain_mesh>& holes, const surface_index& index,
                                           const glm::vec3& position) {
    if (index.hole_count != holes.size() || index.hole_starts.empty()) {
        return sample_holes(holes, position);
    }
    const index_range among = cell_items(index, index.hole_starts, index.hole_items, position);
    return sample_holes(holes, position, &among);
}

std::optional<zone_hit> winning_zone_at(const std::vector<material_zone>& zones, const surface_index& index,
                                        const glm::vec3& position) {
    if (index.zone_count != zones.size() || index.zone_starts.empty()) {
        return winning_zone_at(zones, position);
    }
    const index_range among = cell_items(index, index.zone_starts, index.zone_items, position);
    return winning_zone_at(zones, position, &among);
}
