#include "physics/ground_mesh.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <utility>

#include <glm/common.hpp>

namespace {
// Caps the grid so a huge or malformed course cannot allocate without bound.
constexpr int max_ground_grid_side = 1024;
// Triangles are emitted in square tiles of this many cells, so consecutive
// triangles stay close together and render chunks cull well.
constexpr int ground_tile_cells = 16;
// Smoothing sweeps over the ground between holes (see build_ground).
constexpr int ground_smoothing_sweeps = 48;

struct grid_bounds {
    glm::vec3 low{std::numeric_limits<float>::max()};
    glm::vec3 high{std::numeric_limits<float>::lowest()};
};

grid_bounds mesh_bounds(const terrain_mesh& mesh) {
    grid_bounds bounds;
    for (const terrain_vertex& vertex : mesh.vertices) {
        bounds.low = glm::min(bounds.low, vertex.position);
        bounds.high = glm::max(bounds.high, vertex.position);
    }
    return bounds;
}

bool contains_xz(const grid_bounds& bounds, const glm::vec3& position) {
    return position.x >= bounds.low.x && position.x <= bounds.high.x &&
        position.z >= bounds.low.z && position.z <= bounds.high.z;
}

// How far inside its ribbon a sample is, from the nearer side edge.
float depth_inside(const terrain_mesh& hole, const terrain_sample& sample) {
    return std::max(0.0f, hole.width * 0.5f - std::abs(sample.distance_from_center));
}

// Whether sample `a` (on `a_hole`) wins over `b` (on `b_hole`) where holes
// overlap: fairway over rough, else the hole the point is deeper inside.
bool outranks(const terrain_mesh& a_hole, const terrain_sample& a, const terrain_mesh& b_hole, const terrain_sample& b) {
    const bool fairway = a.material == terrain_material::fairway;
    const bool other_fairway = b.material == terrain_material::fairway;
    return (fairway && !other_fairway) || (fairway == other_fairway && depth_inside(a_hole, a) > depth_inside(b_hole, b));
}

// Every hole containing a point, with its sample.
struct hole_hit {
    std::size_t hole = 0;
    terrain_sample sample;
};

std::vector<hole_hit> hits_at(const std::vector<terrain_mesh>& holes,
                              const std::vector<grid_bounds>& bounds,
                              const glm::vec3& position) {
    std::vector<hole_hit> hits;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        if (!contains_xz(bounds[i], position)) {
            continue;
        }
        if (std::optional<terrain_sample> sample = sample_terrain_inside(holes[i], position)) {
            hits.push_back(hole_hit{i, *sample});
        }
    }
    return hits;
}

const hole_hit* winning_hit(const std::vector<terrain_mesh>& holes, const std::vector<hole_hit>& hits) {
    const hole_hit* best = nullptr;
    for (const hole_hit& hit : hits) {
        if (best == nullptr || outranks(holes[hit.hole], hit.sample, holes[best->hole], best->sample)) {
            best = &hit;
        }
    }
    return best;
}

// Overlapping holes blend: each weighs in by how deep inside it the point is,
// so the surface is continuous across every hole edge.
float blended_height(const std::vector<terrain_mesh>& holes, const std::vector<hole_hit>& hits) {
    float weighted = 0.0f;
    float total = 0.0f;
    for (const hole_hit& hit : hits) {
        const float half_width = std::max(0.001f, holes[hit.hole].width * 0.5f);
        const float t = clamp01(depth_inside(holes[hit.hole], hit.sample) / half_width);
        const float weight = std::max(0.0001f, t * t * (3.0f - 2.0f * t));
        weighted += hit.sample.point.y * weight;
        total += weight;
    }
    return weighted / total;
}

// Two triangles per cell of a rows x columns vertex grid, tile by tile.
// Two triangles per cell, tile by tile, leaving out the cells `skip` marks
// (row-major over the cells).
std::vector<std::uint32_t> tiled_grid_indices(const int rows, const int columns, const std::vector<bool>& skip) {
    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>((rows - 1) * (columns - 1) * 6));
    for (int tile_row = 0; tile_row < rows - 1; tile_row += ground_tile_cells) {
        for (int tile_column = 0; tile_column < columns - 1; tile_column += ground_tile_cells) {
            for (int row = tile_row; row < std::min(tile_row + ground_tile_cells, rows - 1); ++row) {
                for (int column = tile_column; column < std::min(tile_column + ground_tile_cells, columns - 1); ++column) {
                    if (skip[static_cast<std::size_t>(row * (columns - 1) + column)]) {
                        continue;
                    }
                    const std::uint32_t a = static_cast<std::uint32_t>(row * columns + column);
                    const std::uint32_t b = static_cast<std::uint32_t>((row + 1) * columns + column);
                    indices.insert(indices.end(), {a, b, a + 1U, a + 1U, b, b + 1U});
                }
            }
        }
    }
    return indices;
}

// Each vertex without a height takes the height of the nearest vertex that
// has one (breadth-first, 4-connected); `steps` counts the grid steps to it.
// Vertices stay unset only when none has a height.
struct grid_spread {
    std::vector<std::optional<float>> heights;
    std::vector<int> steps;
};

grid_spread spread_grid_heights(std::vector<std::optional<float>> heights, const int rows, const int columns) {
    grid_spread spread;
    spread.steps.assign(heights.size(), 0);
    std::vector<int> frontier;
    for (int i = 0; i < static_cast<int>(heights.size()); ++i) {
        if (heights[static_cast<std::size_t>(i)]) {
            frontier.push_back(i);
        }
    }
    for (std::size_t next = 0; next < frontier.size(); ++next) {
        const int vertex = frontier[next];
        const int row = vertex / columns;
        const int column = vertex % columns;
        const std::array<std::array<int, 2>, 4> neighbours{{{row - 1, column}, {row + 1, column}, {row, column - 1}, {row, column + 1}}};
        for (const std::array<int, 2>& neighbour : neighbours) {
            if (neighbour[0] < 0 || neighbour[0] >= rows || neighbour[1] < 0 || neighbour[1] >= columns) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(neighbour[0] * columns + neighbour[1]);
            if (!heights[index]) {
                heights[index] = heights[static_cast<std::size_t>(vertex)];
                spread.steps[index] = spread.steps[static_cast<std::size_t>(vertex)] + 1;
                frontier.push_back(static_cast<int>(index));
            }
        }
    }
    spread.heights = std::move(heights);
    return spread;
}
}

float sample_height_grid(const height_grid& grid, const float x, const float z) {
    if (grid.columns < 1 || grid.rows < 1 || grid.heights.size() < static_cast<std::size_t>(grid.columns * grid.rows)) {
        return 0.0f;
    }
    const float cell = std::max(0.0001f, grid.cell_size);
    const float gx = std::clamp((x - grid.origin_x) / cell, 0.0f, static_cast<float>(grid.columns - 1));
    const float gz = std::clamp((z - grid.origin_z) / cell, 0.0f, static_cast<float>(grid.rows - 1));
    const int column = std::min(static_cast<int>(gx), std::max(0, grid.columns - 2));
    const int row = std::min(static_cast<int>(gz), std::max(0, grid.rows - 2));
    const int next_column = std::min(column + 1, grid.columns - 1);
    const int next_row = std::min(row + 1, grid.rows - 1);
    const auto at = [&grid](const int r, const int c) { return grid.heights[static_cast<std::size_t>(r * grid.columns + c)]; };
    const float tx = gx - static_cast<float>(column);
    const float tz = gz - static_cast<float>(row);
    const float near_row = at(row, column) + (at(row, next_column) - at(row, column)) * tx;
    const float far_row = at(next_row, column) + (at(next_row, next_column) - at(next_row, column)) * tx;
    return near_row + (far_row - near_row) * tz;
}

std::optional<terrain_sample> sample_holes(const std::vector<terrain_mesh>& holes, const glm::vec3& position) {
    std::optional<terrain_sample> best;
    std::size_t best_hole = 0;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        const std::optional<terrain_sample> sample = sample_terrain_inside(holes[i], position);
        if (!sample) {
            continue;
        }
        if (!best || outranks(holes[i], *sample, holes[best_hole], *best)) {
            best = sample;
            best_hole = i;
        }
    }
    return best;
}

terrain_material surface_material(const std::vector<material_zone>& zones,
                                  const terrain_sample* on_hole,
                                  const glm::vec3& position) {
    return zone_material_at(zones, position).value_or(on_hole != nullptr ? on_hole->material : terrain_material::rough);
}

terrain_mesh build_ground(const std::vector<terrain_mesh>& holes,
                          const std::vector<material_zone>& zones,
                          const height_grid* land,
                          const ground_settings& settings) {
    terrain_mesh ground;
    std::vector<grid_bounds> hole_bounds;
    grid_bounds bounds;
    for (const terrain_mesh& hole : holes) {
        hole_bounds.push_back(mesh_bounds(hole));
        bounds.low = glm::min(bounds.low, hole_bounds.back().low);
        bounds.high = glm::max(bounds.high, hole_bounds.back().high);
    }
    const float margin = std::max(0.0f, settings.margin);
    bounds.low -= glm::vec3(margin, 0.0f, margin);
    bounds.high += glm::vec3(margin, 0.0f, margin);
    if (land != nullptr && land->columns > 1 && land->rows > 1) {
        bounds.low = glm::min(bounds.low, glm::vec3(land->origin_x, 0.0f, land->origin_z));
        bounds.high = glm::max(bounds.high,
                               glm::vec3(land->origin_x + land->cell_size * static_cast<float>(land->columns - 1),
                                         0.0f,
                                         land->origin_z + land->cell_size * static_cast<float>(land->rows - 1)));
    }
    const glm::vec3 low = bounds.low;
    const glm::vec3 high = bounds.high;
    if (!(low.x < high.x) || !(low.z < high.z)) {
        return ground;
    }

    const float cell = std::max(0.5f, settings.cell_size);
    const auto grid_side = [cell](const float span) {
        return std::clamp(static_cast<int>(std::ceil(span / cell)) + 1, 2, max_ground_grid_side);
    };
    const int rows = grid_side(high.z - low.z);
    const int columns = grid_side(high.x - low.x);
    ground.width = std::max(high.x - low.x, high.z - low.z);
    const float step_x = (high.x - low.x) / static_cast<float>(columns - 1);
    const float step_z = (high.z - low.z) / static_cast<float>(rows - 1);
    const std::size_t vertex_count = static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns);
    const auto grid_point = [&](const int row, const int column) {
        return glm::vec3(low.x + step_x * static_cast<float>(column), 0.0f, low.z + step_z * static_cast<float>(row));
    };

    // On the holes: the blended hole height. Materials as surface_material.
    std::vector<bool> on_hole(vertex_count, false);
    std::vector<float> base(vertex_count, 0.0f);
    std::vector<std::optional<float>> offsets(vertex_count);
    std::vector<terrain_material> materials(vertex_count, terrain_material::rough);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * columns + column);
            const glm::vec3 point = grid_point(row, column);
            base[index] = land != nullptr ? sample_height_grid(*land, point.x, point.z) : 0.0f;
            const std::vector<hole_hit> hits = hits_at(holes, hole_bounds, point);
            const hole_hit* best = winning_hit(holes, hits);
            if (best != nullptr) {
                on_hole[index] = true;
                offsets[index] = blended_height(holes, hits) - base[index];
            }
            materials[index] = surface_material(zones, best != nullptr ? &best->sample : nullptr, point);
        }
    }

    // Off the holes: each hole's offset from the land (or its height, without
    // land) diffuses outward and, with land, fades to nothing over the blend
    // distance. A nearest-hole rule would leave a cliff wherever two holes'
    // areas meet; smoothing has no such line. The spread is the first guess,
    // the sweeps relax it towards a smooth surface.
    const grid_spread spread = spread_grid_heights(std::move(offsets), rows, columns);
    std::vector<float> offset(vertex_count, 0.0f);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        const float fade = land != nullptr && !on_hole[i] && settings.blend_distance > 0.0f
            ? clamp01(static_cast<float>(spread.steps[i]) * cell / settings.blend_distance)
            : 0.0f;
        offset[i] = spread.heights[i].value_or(0.0f) * (1.0f - fade * fade * (3.0f - 2.0f * fade));
    }
    // Screened smoothing: with land, offsets decay over about half the blend
    // distance; without it they only smooth.
    const float decay_length = std::max(cell, settings.blend_distance * 0.5f);
    const float screening = land != nullptr ? (cell * cell) / (decay_length * decay_length) : 0.0f;
    for (int sweep = 0; sweep < ground_smoothing_sweeps; ++sweep) {
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const std::size_t index = static_cast<std::size_t>(row * columns + column);
                if (on_hole[index]) {
                    continue;
                }
                float sum = 0.0f;
                float count = 0.0f;
                if (row > 0) { sum += offset[index - static_cast<std::size_t>(columns)]; count += 1.0f; }
                if (row + 1 < rows) { sum += offset[index + static_cast<std::size_t>(columns)]; count += 1.0f; }
                if (column > 0) { sum += offset[index - 1U]; count += 1.0f; }
                if (column + 1 < columns) { sum += offset[index + 1U]; count += 1.0f; }
                offset[index] = sum / (count + screening);
            }
        }
    }

    // The cells over a bunker or pond, and a cell beyond, so the carve is
    // nothing at their outer edges: only their corners are carved, and when
    // they are split finer they meet their neighbours without a step.
    const int cell_columns = columns - 1;
    std::vector<bool> near_zone(static_cast<std::size_t>((rows - 1) * cell_columns), false);
    // Every zone whose box reaches a cell, by cell: all that can decide a
    // point in it, so a fine point there looks at those alone.
    std::unordered_map<std::size_t, std::vector<material_zone>> cell_zones;
    for (const material_zone& zone : zones) {
        const glm::vec2 reach = zone_half_extent(zone);
        const int column0 = std::clamp(static_cast<int>(std::floor((zone.center.x - reach.x - low.x) / step_x)), 0, cell_columns - 1);
        const int column1 = std::clamp(static_cast<int>(std::floor((zone.center.x + reach.x - low.x) / step_x)), 0, cell_columns - 1);
        const int row0 = std::clamp(static_cast<int>(std::floor((zone.center.z - reach.y - low.z) / step_z)), 0, rows - 2);
        const int row1 = std::clamp(static_cast<int>(std::floor((zone.center.z + reach.y - low.z) / step_z)), 0, rows - 2);
        for (int row = row0; row <= row1; ++row) {
            for (int column = column0; column <= column1; ++column) {
                cell_zones[static_cast<std::size_t>(row * cell_columns + column)].push_back(zone);
            }
        }
    }
    for (const material_zone& zone : zones) {
        if (zone.type != material_zone_type::bunker && zone.type != material_zone_type::water) {
            continue;
        }
        const glm::vec2 half = zone_half_extent(zone);
        const auto cells = [](const float from, const float to, const float origin, const float step, const int count) {
            return std::pair<int, int>(std::clamp(static_cast<int>(std::floor((from - origin) / step)) - 1, 0, count - 1),
                                       std::clamp(static_cast<int>(std::floor((to - origin) / step)) + 1, 0, count - 1));
        };
        const auto [first_column, last_column] = cells(zone.center.x - half.x, zone.center.x + half.x, low.x, step_x, cell_columns);
        const auto [first_row, last_row] = cells(zone.center.z - half.y, zone.center.z + half.y, low.z, step_z, rows - 1);
        for (int row = first_row; row <= last_row; ++row) {
            for (int column = first_column; column <= last_column; ++column) {
                near_zone[static_cast<std::size_t>(row * cell_columns + column)] = true;
            }
        }
    }
    const auto corner_of_near_cell = [&](const int row, const int column) {
        for (int r = std::max(0, row - 1); r <= std::min(row, rows - 2); ++r) {
            for (int c = std::max(0, column - 1); c <= std::min(column, cell_columns - 1); ++c) {
                if (near_zone[static_cast<std::size_t>(r * cell_columns + c)]) {
                    return true;
                }
            }
        }
        return false;
    };
    const int split = settings.zone_cell_size > 0.0f
        ? std::max(1, static_cast<int>(std::ceil(std::max(step_x, step_z) / settings.zone_cell_size)))
        : 1;

    std::vector<float> height(vertex_count, 0.0f);  // before carving
    std::vector<terrain_vertex> vertices;
    vertices.reserve(vertex_count);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * columns + column);
            height[index] = base[index] + offset[index];
            terrain_vertex vertex;
            vertex.position = grid_point(row, column);
            vertex.position.y = height[index];
            if (corner_of_near_cell(row, column)) {
                vertex.position.y -= zone_carve_depth(zones, vertex.position, settings.zones);
            }
            vertex.material = materials[index];
            vertices.push_back(vertex);
        }
    }

    const std::vector<bool> fine = split > 1 ? near_zone : std::vector<bool>(near_zone.size(), false);
    ground.indices = tiled_grid_indices(rows, columns, fine);

    // Fine points are shared between neighbouring fine cells (a coarse grid
    // point is its own vertex), each at the cell's bilinear height, carved.
    const long long fine_columns = static_cast<long long>(cell_columns) * split + 1;
    std::unordered_map<long long, std::uint32_t> fine_vertices;
    const std::vector<material_zone> no_zones;
    const auto fine_vertex = [&](const int fine_row, const int fine_column) -> std::uint32_t {
        if (fine_row % split == 0 && fine_column % split == 0) {
            return static_cast<std::uint32_t>((fine_row / split) * columns + fine_column / split);
        }
        const long long key = static_cast<long long>(fine_row) * fine_columns + fine_column;
        if (const auto found = fine_vertices.find(key); found != fine_vertices.end()) {
            return found->second;
        }
        const int row = std::min(fine_row / split, rows - 2);
        const int column = std::min(fine_column / split, cell_columns - 1);
        const auto local = cell_zones.find(static_cast<std::size_t>(row * cell_columns + column));
        const std::vector<material_zone>& here = local != cell_zones.end() ? local->second : no_zones;
        const float u = static_cast<float>(fine_column - column * split) / static_cast<float>(split);
        const float v = static_cast<float>(fine_row - row * split) / static_cast<float>(split);
        const auto at = [&](const int r, const int c) { return height[static_cast<std::size_t>(r * columns + c)]; };
        const float y = (at(row, column) * (1.0f - u) + at(row, column + 1) * u) * (1.0f - v) +
            (at(row + 1, column) * (1.0f - u) + at(row + 1, column + 1) * u) * v;
        terrain_vertex vertex;
        vertex.position = glm::vec3(low.x + step_x * (static_cast<float>(column) + u), 0.0f,
                                    low.z + step_z * (static_cast<float>(row) + v));
        const std::vector<hole_hit> hits = hits_at(holes, hole_bounds, vertex.position);
        const hole_hit* best = winning_hit(holes, hits);
        vertex.material = surface_material(here, best != nullptr ? &best->sample : nullptr, vertex.position);
        vertex.position.y = y - zone_carve_depth(here, vertex.position, settings.zones);
        vertices.push_back(vertex);
        const std::uint32_t index = static_cast<std::uint32_t>(vertices.size() - 1);
        fine_vertices.emplace(key, index);
        return index;
    };
    for (int row = 0; row < rows - 1; ++row) {
        for (int column = 0; column < cell_columns; ++column) {
            if (!fine[static_cast<std::size_t>(row * cell_columns + column)]) {
                continue;
            }
            for (int i = 0; i < split; ++i) {
                for (int j = 0; j < split; ++j) {
                    const int fine_row = row * split + i;
                    const int fine_column = column * split + j;
                    const std::uint32_t a = fine_vertex(fine_row, fine_column);
                    const std::uint32_t a1 = fine_vertex(fine_row, fine_column + 1);
                    const std::uint32_t b = fine_vertex(fine_row + 1, fine_column);
                    const std::uint32_t b1 = fine_vertex(fine_row + 1, fine_column + 1);
                    ground.indices.insert(ground.indices.end(), {a, b, a1, a1, b, b1});
                }
            }
        }
    }

    ground.vertices = with_smooth_normals(std::move(vertices), ground.indices);
    return build_terrain_mesh_index(std::move(ground));
}
