#include "renderer/grass_baker.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "renderer/grass_field.h"
#include "renderer/grass_tiles.h"

namespace {
// A tile's nodes and the ring round them, which its edge nodes' cells need.
constexpr int side = grass_tile_nodes + 2;
constexpr float no_grass_distance = grass_field_distance_cap * grass_field_distance_cap;
const glm::vec3 empty_node(grass_no_ground_height, no_grass_distance, 0.0f);

float cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }
int floor_div(const int a, const int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct corner {
    glm::vec2 at;
    float height;
    float blades;
};
using triangle_corners = std::array<corner, 3>;

corner corner_of(const render_terrain_vertex& v) { return {glm::vec2(v.position.x, v.position.z), v.position.y, v.blades()}; }

triangle_corners triangle_of(const render_static_mesh& mesh, const std::size_t first) {
    return {corner_of(mesh.vertices[mesh.indices[first]]), corner_of(mesh.vertices[mesh.indices[first + 1]]),
            corner_of(mesh.vertices[mesh.indices[first + 2]])};
}

bool bare(const triangle_corners& t) { return t[0].blades < 0.5f && t[1].blades < 0.5f && t[2].blades < 0.5f; }
bool rough(const triangle_corners& t) { return t[0].blades >= 1.5f && t[1].blades >= 1.5f && t[2].blades >= 1.5f; }

// What a triangle writes to the nodes it covers.
enum stamp_part : unsigned { stamp_height = 1, stamp_kind = 2, stamp_bare = 4 };

// The lattice nodes (columns, rows from the origin) inside the triangle's
// bounds; empty when it is degenerate or reaches none.
struct node_range {
    glm::ivec2 low{0};
    glm::ivec2 high{-1};
    bool empty() const { return high.x < low.x || high.y < low.y; }
};

node_range nodes_under(const triangle_corners& t, const glm::vec2& origin, const int columns, const int rows) {
    const float area = cross(t[1].at - t[0].at, t[2].at - t[0].at);
    if (std::abs(area) < 1.0e-10f) { return {}; }
    const glm::vec2 low = (glm::min(t[0].at, glm::min(t[1].at, t[2].at)) - origin) / grass_field_spacing;
    const glm::vec2 high = (glm::max(t[0].at, glm::max(t[1].at, t[2].at)) - origin) / grass_field_spacing;
    return {glm::ivec2(std::max(0, static_cast<int>(std::ceil(low.x))), std::max(0, static_cast<int>(std::ceil(low.y)))),
            glm::ivec2(std::min(columns - 1, static_cast<int>(std::floor(high.x))), std::min(rows - 1, static_cast<int>(std::floor(high.y))))};
}

// The tiles whose ring of nodes (cells -1 to 65 of the tile) holds some of
// `range`: x, y least, z, w greatest.
glm::ivec4 tiles_over(const node_range& range, const glm::ivec2& first_cell) {
    const glm::ivec2 low = range.low + first_cell;
    const glm::ivec2 high = range.high + first_cell;
    return glm::ivec4(floor_div(low.x - 2, grass_tile_cells), floor_div(low.y - 2, grass_tile_cells),
                      floor_div(high.x + 1, grass_tile_cells), floor_div(high.y + 1, grass_tile_cells));
}

// Writes `t` into `grid`, whose node (0, 0) is lattice node `base`.
void stamp(std::vector<glm::vec3>& grid, const glm::ivec2& base, const glm::vec2& origin, const node_range& range,
           const triangle_corners& t, const unsigned parts) {
    const float area = cross(t[1].at - t[0].at, t[2].at - t[0].at);
    const glm::ivec2 low = glm::max(range.low, base);
    const glm::ivec2 high = glm::min(range.high, base + glm::ivec2(side - 1));
    for (int z = low.y; z <= high.y; ++z) {
        for (int x = low.x; x <= high.x; ++x) {
            const glm::vec2 p = origin + glm::vec2(x, z) * grass_field_spacing;
            const float wb = cross(p - t[0].at, t[2].at - t[0].at) / area;
            const float wc = cross(t[1].at - t[0].at, p - t[0].at) / area;
            const float wa = 1.0f - wb - wc;
            if (std::min(wa, std::min(wb, wc)) < -1.0e-5f) { continue; }
            glm::vec3& node = grid[static_cast<std::size_t>(z - base.y) * side + (x - base.x)];
            if (parts & stamp_height) { node.x = wa * t[0].height + wb * t[1].height + wc * t[2].height; }
            if (parts & stamp_kind) { node.z = std::max(1.0f, wa * t[0].blades + wb * t[1].blades + wc * t[2].blades); }
            if (parts & stamp_bare) { node.y = 0.0f; }
        }
    }
}
}

grass_lattice grass_lattice_of(const render_static_mesh& ground) {
    grass_lattice lattice;
    if (!ground.bounds.valid || ground.indices.empty()) { return lattice; }
    lattice.origin = glm::floor(glm::vec2(ground.bounds.min.x, ground.bounds.min.z) / grass_field_spacing) * grass_field_spacing;
    const glm::vec2 end(ground.bounds.max.x, ground.bounds.max.z);
    const glm::ivec2 size = glm::ivec2(glm::ceil((end - lattice.origin) / grass_field_spacing)) + 1;
    lattice.columns = size.x;
    lattice.rows = size.y;
    return lattice;
}

grass_baker::grass_baker(const render_static_mesh& ground, const render_static_mesh& overlay, const std::vector<tee_box>& tees)
    : ground_(ground), overlay_(overlay) {
    const grass_lattice lattice = grass_lattice_of(ground);
    if (lattice.columns < 2) { return; }
    origin_ = lattice.origin;
    columns_ = lattice.columns;
    rows_ = lattice.rows;
    const grass_tile_extent extent = grass_tile_extent_of(origin_, columns_, rows_);
    first_cell_ = extent.first_cell;
    first_tile_ = extent.first_tile;
    tile_counts_ = extent.counts;

    for (const tee_box& tee : tees) {
        const glm::vec3 along = tee.down_hole * tee.half_length;
        const glm::vec3 across(-tee.down_hole.z * tee.half_width, 0.0f, tee.down_hole.x * tee.half_width);
        const glm::vec3 c[4] = {tee.center - along - across, tee.center + along - across, tee.center + along + across, tee.center - along + across};
        for (int i = 0; i < 4; ++i) { distance_.add({c[i].x, c[i].z}, {c[(i + 1) % 4].x, c[(i + 1) % 4].z}); }
        tees_.push_back({glm::vec2(c[0].x, c[0].z), glm::vec2(c[1].x, c[1].z), glm::vec2(c[2].x, c[2].z)});
        tees_.push_back({glm::vec2(c[0].x, c[0].z), glm::vec2(c[2].x, c[2].z), glm::vec2(c[3].x, c[3].z)});
    }
    for (const render_static_mesh* mesh : {&ground, &overlay}) {
        for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
            const triangle_corners t = triangle_of(*mesh, i);
            if (!bare(t)) { continue; }
            for (int k = 0; k < 3; ++k) { distance_.add(t[k].at, t[(k + 1) % 3].at); }
        }
    }
    distance_.build();

    // The triangles that can reach each tile, in mesh order: counted, then placed.
    const std::size_t tile_total = static_cast<std::size_t>(tile_counts_.x) * tile_counts_.y;
    const auto index_mesh = [&](const render_static_mesh& mesh, const bool every, triangle_lists& lists) {
        lists.first.assign(tile_total + 1, 0);
        std::vector<std::uint32_t> cursor;
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) {
                for (std::size_t i = 0; i < tile_total; ++i) { lists.first[i + 1] += lists.first[i]; }
                lists.items.resize(lists.first[tile_total]);
                cursor.assign(lists.first.begin(), lists.first.end() - 1);
            }
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const triangle_corners t = triangle_of(mesh, i);
                if (!every && !bare(t) && !rough(t)) { continue; }
                const node_range range = nodes_under(t, origin_, columns_, rows_);
                if (range.empty()) { continue; }
                const glm::ivec4 tiles = tiles_over(range, first_cell_);
                for (int tz = std::max(tiles.y, first_tile_.y); tz <= std::min(tiles.w, first_tile_.y + tile_counts_.y - 1); ++tz) {
                    for (int tx = std::max(tiles.x, first_tile_.x); tx <= std::min(tiles.z, first_tile_.x + tile_counts_.x - 1); ++tx) {
                        const std::size_t slot = static_cast<std::size_t>(tz - first_tile_.y) * tile_counts_.x + (tx - first_tile_.x);
                        if (pass == 0) { ++lists.first[slot + 1]; }
                        else { lists.items[cursor[slot]++] = static_cast<std::uint32_t>(i); }
                    }
                }
            }
        }
    };
    index_mesh(ground, true, ground_lists_);
    index_mesh(overlay, false, overlay_lists_);
}

std::size_t grass_baker::index_bytes() const {
    const std::size_t lists = (ground_lists_.first.capacity() + ground_lists_.items.capacity() + overlay_lists_.first.capacity() +
                               overlay_lists_.items.capacity()) * sizeof(std::uint32_t);
    return std::max(distance_.peak_bytes(), distance_.bytes() + lists) + grid_.capacity() * sizeof(glm::vec3);
}

bool grass_baker::bake(const glm::ivec2& tile, grass_exact_tile& out) {
    const glm::ivec2 at = tile - first_tile_;
    if (columns_ < 2 || at.x < 0 || at.y < 0 || at.x >= tile_counts_.x || at.y >= tile_counts_.y) { return false; }
    const std::size_t slot = static_cast<std::size_t>(at.y) * tile_counts_.x + at.x;
    if (ground_lists_.first[slot] == ground_lists_.first[slot + 1]) { return false; }

    // Node (0, 0) of the grid is the lattice node a ring's width before the tile's own.
    const glm::ivec2 base = tile * grass_tile_cells - first_cell_ - 1;
    grid_.assign(static_cast<std::size_t>(side) * side, empty_node);
    for (std::uint32_t i = ground_lists_.first[slot]; i < ground_lists_.first[slot + 1]; ++i) {
        const triangle_corners t = triangle_of(ground_, ground_lists_.items[i]);
        stamp(grid_, base, origin_, nodes_under(t, origin_, columns_, rows_), t, stamp_height | stamp_kind | (bare(t) ? stamp_bare : 0u));
    }
    for (std::uint32_t i = overlay_lists_.first[slot]; i < overlay_lists_.first[slot + 1]; ++i) {
        const triangle_corners t = triangle_of(overlay_, overlay_lists_.items[i]);
        stamp(grid_, base, origin_, nodes_under(t, origin_, columns_, rows_), t, rough(t) ? stamp_kind : stamp_bare);
    }
    for (const auto& tee : tees_) {
        const triangle_corners t{corner{tee[0], 0.0f, 0.0f}, corner{tee[1], 0.0f, 0.0f}, corner{tee[2], 0.0f, 0.0f}};
        stamp(grid_, base, origin_, nodes_under(t, origin_, columns_, rows_), t, stamp_bare);
    }

    const auto in_field = [&](const int x, const int z) {
        return base.x + x >= 0 && base.y + z >= 0 && base.x + x < columns_ && base.y + z < rows_;
    };
    // No blades outside the ground; keep its border conservative too.
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            if (!in_field(x, z)) { continue; }
            glm::vec3& node = grid_[static_cast<std::size_t>(z) * side + x];
            const glm::ivec2 lattice = base + glm::ivec2(x, z);
            const float border = grass_field_spacing * static_cast<float>(std::min({lattice.x, lattice.y, columns_ - 1 - lattice.x, rows_ - 1 - lattice.y}));
            if (node.z == 0.0f) { node.y = 0.0f; }
            else if (node.y > 0.0f) { node.y = std::sqrt(std::min(distance_.squared(origin_ + glm::vec2(lattice) * grass_field_spacing), border * border)); }
        }
    }
    // A blade over a cell with a node that has no ground would stand at a height
    // interpolated from nothing: none grows there.
    for (int z = 0; z + 1 < side; ++z) {
        for (int x = 0; x + 1 < side; ++x) {
            if (!in_field(x, z) || !in_field(x + 1, z + 1)) { continue; }
            const std::size_t i = static_cast<std::size_t>(z) * side + x;
            const std::size_t cell[4] = {i, i + 1, i + side, i + side + 1};
            if (grid_[cell[0]].z == 0.0f || grid_[cell[1]].z == 0.0f || grid_[cell[2]].z == 0.0f || grid_[cell[3]].z == 0.0f) {
                for (const std::size_t node : cell) { grid_[node].y = 0.0f; }
            }
        }
    }

    const std::size_t count = static_cast<std::size_t>(grass_tile_nodes) * grass_tile_nodes;
    out.height.resize(count);
    out.clearance.resize(count);
    out.kind.resize(count);
    for (int z = 0; z < grass_tile_nodes; ++z) {
        for (int x = 0; x < grass_tile_nodes; ++x) {
            // Past the field a node repeats its nearest edge node's height and kind, with no clearance.
            const glm::ivec2 lattice = base + glm::ivec2(x + 1, z + 1);
            const glm::ivec2 nearest = glm::clamp(lattice, glm::ivec2(0), glm::ivec2(columns_ - 1, rows_ - 1));
            const glm::ivec2 local = nearest - base;
            const glm::vec3& node = grid_[static_cast<std::size_t>(local.y) * side + local.x];
            const std::size_t i = static_cast<std::size_t>(z) * grass_tile_nodes + x;
            out.height[i] = node.x;
            out.clearance[i] = nearest == lattice ? node.y : 0.0f;
            out.kind[i] = node.z;
        }
    }
    return true;
}
