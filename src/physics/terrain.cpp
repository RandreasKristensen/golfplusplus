#include "physics/terrain.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

namespace {
// Ribbon meshes have this many vertices across.
constexpr int ribbon_cross_section_count = 9;
// The ribbon's outer 45% drops away by ribbon_edge_drop (terrain.h).
constexpr float ribbon_flat_fraction = 0.55f;
// Fewest rim vertices of a round zone overlay, however small the zone.
constexpr int overlay_circle_segments = 32;
constexpr float water_edge_softness = 0.25f;

float smoothstep(const float edge0, const float edge1, const float value) {
    const float t = clamp01((value - edge0) / std::max(0.00001f, edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float distance_xz_squared(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 delta = horizontal(a - b);
    return glm::dot(delta, delta);
}

float distance_squared(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 delta = a - b;
    return glm::dot(delta, delta);
}

float control_polygon_xz_length(const std::vector<glm::vec3>& points) {
    float length = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        length += horizontal_distance(points[i], points[i - 1]);
    }
    return length;
}

// Higher wins when zones overlap. The importer fits greens and bunkers as
// circles of their real area but water as the bounding box of a pond or creek,
// which can cover dry ground around it, so the precise shapes win over water.
int material_priority(const terrain_material material) {
    switch (material) {
    case terrain_material::bunker:
        return 3;
    case terrain_material::green:
        return 2;
    case terrain_material::water:
        return 1;
    case terrain_material::fairway:
        return 0;
    case terrain_material::rough:
        return -1;
    }
    return -1;
}

std::optional<terrain_material> material_from_zone(const material_zone_type type) {
    switch (type) {
    case material_zone_type::green:
        return terrain_material::green;
    case material_zone_type::bunker:
        return terrain_material::bunker;
    case material_zone_type::water:
        return terrain_material::water;
    case material_zone_type::unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

// Normalized distance from the zone centre (0 at the centre, 1 at the edge),
// or nullopt when `position` is outside the zone.
std::optional<float> zone_normalized_distance(const material_zone& zone, const glm::vec3& position) {
    if (zone.has_radius && zone.radius > 0.00001f) {
        const float distance_squared = distance_xz_squared(position, zone.center);
        if (distance_squared > zone.radius * zone.radius) {
            return std::nullopt;
        }
        return clamp01(std::sqrt(distance_squared) / zone.radius);
    }

    if (zone.has_bounds) {
        const glm::vec3 low = glm::min(zone.bounds_min, zone.bounds_max);
        const glm::vec3 high = glm::max(zone.bounds_min, zone.bounds_max);
        const glm::vec3 center = (low + high) * 0.5f;
        const glm::vec3 half = glm::max((high - low) * 0.5f, glm::vec3(0.0001f));
        // Outside the circle around the box, whatever its rotation. Most
        // queries stop here, before the rotation's sine and cosine.
        if (distance_xz_squared(position, center) > half.x * half.x + half.z * half.z) {
            return std::nullopt;
        }
        // The query in the box's own frame, before its rotation.
        const glm::vec3 local = rotate_about_y(horizontal(position - center), -zone.rotation);
        if (std::abs(local.x) > half.x || std::abs(local.z) > half.z) {
            return std::nullopt;
        }
        return clamp01(std::max(std::abs(local.x) / half.x, std::abs(local.z) / half.z));
    }

    return std::nullopt;
}

struct zone_hit {
    terrain_material material = terrain_material::fairway;
    float normalized_distance = 1.0f;
};

// The highest-priority zone at `position`; among equals, the one whose centre
// is nearest. Zones of unknown type are ignored.
std::optional<zone_hit> query_zone_hit(const glm::vec3& position, const std::vector<material_zone>& zones) {
    std::optional<zone_hit> best;
    for (const material_zone& zone : zones) {
        const std::optional<terrain_material> material = material_from_zone(zone.type);
        const std::optional<float> distance = zone_normalized_distance(zone, position);
        if (!material || !distance) {
            continue;
        }
        if (best) {
            const int priority = material_priority(*material);
            const int best_priority = material_priority(best->material);
            if (priority < best_priority || (priority == best_priority && *distance >= best->normalized_distance)) {
                continue;
            }
        }
        best = zone_hit{*material, *distance};
    }
    return best;
}

glm::vec3 catmull_rom(const glm::vec3& p0,
                      const glm::vec3& p1,
                      const glm::vec3& p2,
                      const glm::vec3& p3,
                      const float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1)
        + (-p0 + p2) * t
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

glm::vec3 lateral_from_tangent(const glm::vec3& tangent) {
    return safe_normalize(glm::vec3(-tangent.z, 0.0f, tangent.x), glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 terrain_lateral_at(const terrain_spline& terrain, const float t) {
    const float step = 1.0f / static_cast<float>(std::max(terrain.sample_count, 2) * 4);
    const glm::vec3 before = sample_terrain_spline_point(terrain, clamp01(t - step));
    const glm::vec3 after = sample_terrain_spline_point(terrain, clamp01(t + step));
    return lateral_from_tangent(safe_normalize(after - before, glm::vec3(0.0f, 0.0f, 1.0f)));
}

// Straight extension direction past the first (or last) control point.
glm::vec3 endpoint_extension_tangent(const terrain_spline& terrain, const bool end) {
    const std::size_t count = terrain.control_points.size();
    const glm::vec3 a = end ? terrain.control_points[count - 2U] : terrain.control_points[0];
    const glm::vec3 b = end ? terrain.control_points[count - 1U] : terrain.control_points[1];
    const glm::vec3 delta = b - a;
    const float xz_length = glm::length(horizontal(delta));
    return xz_length <= 0.00001f ? glm::vec3(0.0f, 0.0f, 1.0f) : delta / xz_length;
}

// Sections: `cap_sections` straight ones before the spline start, the spline
// itself, then `cap_sections` after its end, so the ribbon reaches past the
// tee and pin by half its width.
struct terrain_section_layout {
    int base_sections = 0;
    int cap_sections = 0;
    float cap_extension = 0.0f;
};

terrain_section_layout make_section_layout(const terrain_spline& terrain) {
    terrain_section_layout layout;
    const int length_sections = static_cast<int>(std::ceil(control_polygon_xz_length(terrain.control_points))) + 1;
    layout.base_sections = std::max(2, std::max(terrain.sample_count, length_sections));
    layout.cap_extension = terrain.width * 0.5f;
    layout.cap_sections = layout.cap_extension <= 0.00001f
        ? 0
        : std::max(1, static_cast<int>(std::ceil(layout.cap_extension)));
    return layout;
}

struct terrain_section_frame {
    glm::vec3 center{0.0f};
    glm::vec3 lateral{1.0f, 0.0f, 0.0f};
    float bank = 0.0f;
};

// The bank at spline parameter t, linear between control points (t maps to
// control points as in sample_terrain_spline_point).
float terrain_bank_at(const terrain_spline& terrain, const float t) {
    const std::vector<float>& bank = terrain.bank;
    if (bank.size() != terrain.control_points.size() || bank.empty()) {
        return 0.0f;
    }
    if (bank.size() == 1) {
        return bank.front();
    }
    const int last = static_cast<int>(bank.size()) - 1;
    const float scaled = clamp01(t) * static_cast<float>(last);
    const int segment = std::min(static_cast<int>(std::floor(scaled)), last - 1);
    const float local_t = scaled - static_cast<float>(segment);
    const float a = bank[static_cast<std::size_t>(segment)];
    const float b = bank[static_cast<std::size_t>(segment + 1)];
    return a + (b - a) * local_t;
}

terrain_section_frame terrain_frame_at_section(const terrain_spline& terrain,
                                               const terrain_section_layout& layout,
                                               const int section) {
    terrain_section_frame frame;
    if (section < layout.cap_sections) {
        const glm::vec3 tangent = endpoint_extension_tangent(terrain, false);
        const float u = static_cast<float>(section) / static_cast<float>(std::max(1, layout.cap_sections));
        frame.center = terrain.control_points.front() - tangent * (layout.cap_extension * (1.0f - u));
        frame.lateral = lateral_from_tangent(tangent);
        frame.bank = terrain_bank_at(terrain, 0.0f);
        return frame;
    }

    const int base_section = section - layout.cap_sections;
    if (base_section < layout.base_sections) {
        const float t = static_cast<float>(base_section) / static_cast<float>(std::max(1, layout.base_sections - 1));
        frame.center = sample_terrain_spline_point(terrain, t);
        frame.lateral = terrain_lateral_at(terrain, t);
        frame.bank = terrain_bank_at(terrain, t);
        return frame;
    }

    const glm::vec3 tangent = endpoint_extension_tangent(terrain, true);
    const int after_section = base_section - layout.base_sections;
    const float u = static_cast<float>(after_section + 1) / static_cast<float>(std::max(1, layout.cap_sections));
    frame.center = terrain.control_points.back() + tangent * (layout.cap_extension * u);
    frame.lateral = lateral_from_tangent(tangent);
    frame.bank = terrain_bank_at(terrain, 1.0f);
    return frame;
}

float cross_section_offset(const int column, const int column_count, const float width) {
    if (column_count <= 1) {
        return 0.0f;
    }
    const float u = static_cast<float>(column) / static_cast<float>(column_count - 1);
    return (u - 0.5f) * width;
}

float cross_section_height_offset(const float offset, const float width) {
    if (width <= 0.00001f) {
        return 0.0f;
    }
    const float normalized = std::abs(offset) / (width * 0.5f);
    const float edge = std::max(0.0f, normalized - ribbon_flat_fraction) / (1.0f - ribbon_flat_fraction);
    return -ribbon_edge_drop * edge * edge;
}

float zone_height_offset(const zone_hit& hit, const terrain_zone_tuning& tuning) {
    const float t = clamp01(hit.normalized_distance);
    if (hit.material == terrain_material::bunker && tuning.bunker_depth > 0.0f) {
        return -(1.0f - t * t) * tuning.bunker_depth;
    }
    if (hit.material == terrain_material::water && tuning.water_depth > 0.0f) {
        return -tuning.water_depth * (1.0f - smoothstep(1.0f - water_edge_softness, 1.0f, t));
    }
    return 0.0f;
}

glm::vec3 triangle_normal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return safe_normalize(glm::cross(b - a, c - a), world_up);
}

float signed_area_xz(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

// Barycentric weights of `p` in triangle abc (XZ only), or nullopt when p is
// outside it (with a small tolerance) or the triangle is degenerate.
std::optional<glm::vec3> barycentric_xz(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    constexpr float tolerance = 0.0001f;
    const float area = signed_area_xz(a, b, c);
    if (std::abs(area) <= 0.000001f) {
        return std::nullopt;
    }
    const float w0 = signed_area_xz(p, b, c) / area;
    const float w1 = signed_area_xz(p, c, a) / area;
    const float w2 = 1.0f - w0 - w1;
    if (w0 < -tolerance || w1 < -tolerance || w2 < -tolerance) {
        return std::nullopt;
    }
    return glm::vec3(w0, w1, w2);
}

const terrain_vertex& triangle_vertex(const terrain_mesh& mesh, const int triangle_index, const std::size_t corner) {
    return mesh.vertices[mesh.indices[static_cast<std::size_t>(triangle_index) * 3U + corner]];
}

// The material with the largest barycentric weight; ties go to the higher
// priority material.
terrain_material blended_material(const terrain_vertex& a,
                                  const terrain_vertex& b,
                                  const terrain_vertex& c,
                                  const glm::vec3& barycentric) {
    std::array<float, terrain_material_count> weights{};
    weights[static_cast<std::size_t>(a.material)] += barycentric.x;
    weights[static_cast<std::size_t>(b.material)] += barycentric.y;
    weights[static_cast<std::size_t>(c.material)] += barycentric.z;

    terrain_material best = terrain_material::fairway;
    float best_weight = -1.0f;
    int best_priority = -1;
    for (std::size_t i = 0; i < terrain_material_count; ++i) {
        if (weights[i] <= 0.00001f) {
            continue;
        }
        const terrain_material material = static_cast<terrain_material>(i);
        const int priority = material_priority(material);
        if (weights[i] > best_weight + 0.00001f ||
            (std::abs(weights[i] - best_weight) <= 0.00001f && priority > best_priority)) {
            best = material;
            best_weight = weights[i];
            best_priority = priority;
        }
    }
    return best;
}

terrain_sample sample_from_barycentric(const terrain_mesh& mesh,
                                       const int triangle_index,
                                       const glm::vec3& barycentric,
                                       const bool inside_surface) {
    const terrain_vertex& a = triangle_vertex(mesh, triangle_index, 0U);
    const terrain_vertex& b = triangle_vertex(mesh, triangle_index, 1U);
    const terrain_vertex& c = triangle_vertex(mesh, triangle_index, 2U);

    terrain_sample sample;
    sample.point = a.position * barycentric.x + b.position * barycentric.y + c.position * barycentric.z;
    sample.normal = safe_normalize(a.normal * barycentric.x + b.normal * barycentric.y + c.normal * barycentric.z,
                                   triangle_normal(a.position, b.position, c.position));
    sample.distance_from_center = std::abs(a.distance_from_center * barycentric.x
        + b.distance_from_center * barycentric.y
        + c.distance_from_center * barycentric.z);
    sample.triangle_index = triangle_index;
    sample.material = inside_surface ? blended_material(a, b, c, barycentric) : terrain_material::rough;
    sample.inside_surface = inside_surface;
    return sample;
}

glm::vec3 closest_barycentric_on_edge(const glm::vec3& position,
                                      const glm::vec3& a,
                                      const glm::vec3& b,
                                      const glm::vec3& c,
                                      const int edge) {
    const std::array<glm::vec3, 3> points{{a, b, c}};
    const int i0 = edge;
    const int i1 = (edge + 1) % 3;
    const glm::vec3 edge_xz = horizontal(points[static_cast<std::size_t>(i1)] - points[static_cast<std::size_t>(i0)]);
    const glm::vec3 delta_xz = horizontal(position - points[static_cast<std::size_t>(i0)]);
    const float denom = glm::dot(edge_xz, edge_xz);
    const float t = denom > 0.000001f ? clamp01(glm::dot(delta_xz, edge_xz) / denom) : 0.0f;

    glm::vec3 barycentric(0.0f);
    barycentric[i0] = 1.0f - t;
    barycentric[i1] = t;
    return barycentric;
}

// Triangle row of a ribbon mesh. Used to keep consecutive samples on the same
// ribbon (`previous_sample`) and to scan one row as a contiguous range.
int triangle_row(const terrain_mesh& mesh, const int triangle_index) {
    const int triangles_per_row = (mesh.cross_section_count - 1) * 2;
    if (mesh.section_count <= 0 || triangles_per_row <= 0) {
        return 0;
    }
    return std::clamp(triangle_index / triangles_per_row, 0, mesh.section_count - 1);
}

// Centreline point of the vertex row the triangle starts on. Derived from the
// triangle's own vertices so it stays right in meshes that append several
// ribbons (each ribbon has one more vertex row than triangle rows).
glm::vec3 triangle_section_center(const terrain_mesh& mesh, const int triangle_index) {
    const int columns = mesh.cross_section_count;
    if (columns <= 0) {
        return glm::vec3(0.0f);
    }
    const std::size_t first_vertex = mesh.indices[static_cast<std::size_t>(triangle_index) * 3U];
    const std::size_t row = first_vertex / static_cast<std::size_t>(columns);
    const std::size_t center = row * static_cast<std::size_t>(columns) + static_cast<std::size_t>(columns / 2);
    return center < mesh.vertices.size() ? mesh.vertices[center].position : glm::vec3(0.0f);
}

struct terrain_candidate {
    terrain_sample sample;
    int row = 0;
    float section_distance = std::numeric_limits<float>::max();
    float height_distance = std::numeric_limits<float>::max();
    float edge_distance = std::numeric_limits<float>::max();
};

// -1, 0 or 1; values within epsilon count as equal.
int compare_near(const float a, const float b) {
    constexpr float epsilon = 0.00001f;
    if (a < b - epsilon) {
        return -1;
    }
    return a > b + epsilon ? 1 : 0;
}

// Candidates on the preferred row win; then the keys are compared in order;
// the lower triangle index breaks exact ties so the result never depends on
// scan order.
bool ranks_before(const terrain_candidate& candidate,
                  const terrain_candidate& best,
                  const int preferred_row,
                  const std::array<float, 3>& candidate_keys,
                  const std::array<float, 3>& best_keys) {
    const bool candidate_preferred = preferred_row >= 0 && candidate.row == preferred_row;
    const bool best_preferred = preferred_row >= 0 && best.row == preferred_row;
    if (candidate_preferred != best_preferred) {
        return candidate_preferred;
    }
    for (std::size_t i = 0; i < candidate_keys.size(); ++i) {
        const int order = compare_near(candidate_keys[i], best_keys[i]);
        if (order != 0) {
            return order < 0;
        }
    }
    return candidate.sample.triangle_index < best.sample.triangle_index;
}

bool is_better_inside_candidate(const terrain_candidate& candidate, const terrain_candidate& best, const int preferred_row) {
    return ranks_before(candidate, best, preferred_row,
                        {candidate.section_distance, candidate.height_distance, candidate.sample.distance_from_center},
                        {best.section_distance, best.height_distance, best.sample.distance_from_center});
}

bool is_better_edge_candidate(const terrain_candidate& candidate, const terrain_candidate& best, const int preferred_row) {
    return ranks_before(candidate, best, preferred_row,
                        {candidate.edge_distance, candidate.section_distance, candidate.sample.distance_from_center},
                        {best.edge_distance, best.section_distance, best.sample.distance_from_center});
}

// ---------------------------------------------------------------------------
// Spatial index (uniform XZ grid, CSR storage)
// ---------------------------------------------------------------------------

constexpr int max_index_cells_per_axis = 256;
constexpr std::size_t max_index_entries = 8000000U;
// Slack used when pruning the ring search. Must be >= the epsilon of
// compare_near so ties are never pruned away.
constexpr float index_prune_slack = 0.0001f;

struct triangle_xz_bounds {
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
};

float rect_distance_xz_squared(const float x,
                               const float z,
                               const float min_x,
                               const float min_z,
                               const float max_x,
                               const float max_z) {
    const float dx = std::max(0.0f, std::max(min_x - x, x - max_x));
    const float dz = std::max(0.0f, std::max(min_z - z, z - max_z));
    return dx * dx + dz * dz;
}

int index_cell_coord(const float value, const float min_value, const float cell_size, const int cell_count) {
    if (cell_count <= 1 || !(cell_size > 0.0f)) {
        return 0;
    }
    const float relative = (value - min_value) / cell_size;
    if (!(relative > 0.0f)) {
        return 0;
    }
    const float coord = std::floor(relative);
    if (!(coord < static_cast<float>(cell_count - 1))) {
        return cell_count - 1;
    }
    return static_cast<int>(coord);
}

triangle_xz_bounds padded_triangle_bounds(const terrain_mesh& mesh, const int triangle_index) {
    const glm::vec3& a = triangle_vertex(mesh, triangle_index, 0U).position;
    const glm::vec3& b = triangle_vertex(mesh, triangle_index, 1U).position;
    const glm::vec3& c = triangle_vertex(mesh, triangle_index, 2U).position;

    triangle_xz_bounds bounds;
    bounds.min_x = std::min({a.x, b.x, c.x});
    bounds.max_x = std::max({a.x, b.x, c.x});
    bounds.min_z = std::min({a.z, b.z, c.z});
    bounds.max_z = std::max({a.z, b.z, c.z});

    // barycentric_xz accepts a small negative tolerance, so a point marginally
    // outside the raw bounding box can still count as inside the triangle.
    // Pad the box so those triangles are still found in the queried cell.
    const float span = std::max(bounds.max_x - bounds.min_x, bounds.max_z - bounds.min_z);
    const float pad = 0.001f + 0.001f * std::max(0.0f, span);
    bounds.min_x -= pad;
    bounds.max_x += pad;
    bounds.min_z -= pad;
    bounds.max_z += pad;
    return bounds;
}

struct cell_range {
    int x0 = 0;
    int x1 = 0;
    int z0 = 0;
    int z1 = 0;
};

cell_range triangle_cells(const terrain_mesh& mesh, const terrain_mesh_index& index, const int triangle_index) {
    const triangle_xz_bounds bounds = padded_triangle_bounds(mesh, triangle_index);
    cell_range cells;
    cells.x0 = index_cell_coord(bounds.min_x, index.min_x, index.cell_size_x, index.cells_x);
    cells.x1 = index_cell_coord(bounds.max_x, index.min_x, index.cell_size_x, index.cells_x);
    cells.z0 = index_cell_coord(bounds.min_z, index.min_z, index.cell_size_z, index.cells_z);
    cells.z1 = index_cell_coord(bounds.max_z, index.min_z, index.cell_size_z, index.cells_z);
    return cells;
}

std::size_t cell_slot(const terrain_mesh_index& index, const int cx, const int cz) {
    return static_cast<std::size_t>(cz) * static_cast<std::size_t>(index.cells_x) + static_cast<std::size_t>(cx);
}

terrain_mesh_index make_terrain_mesh_index(const terrain_mesh& mesh) {
    terrain_mesh_index built;
    const std::size_t triangle_count = mesh.indices.size() / 3U;
    const std::size_t vertex_count = mesh.vertices.size();
    if (vertex_count == 0U || triangle_count == 0U || triangle_count * 3U != mesh.indices.size() ||
        triangle_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return built;
    }
    for (const std::uint32_t index : mesh.indices) {
        if (static_cast<std::size_t>(index) >= vertex_count) {
            return built;
        }
    }

    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    for (const terrain_vertex& vertex : mesh.vertices) {
        min_x = std::min(min_x, vertex.position.x);
        max_x = std::max(max_x, vertex.position.x);
        min_z = std::min(min_z, vertex.position.z);
        max_z = std::max(max_z, vertex.position.z);
    }
    if (!(min_x <= max_x) || !(min_z <= max_z)) {
        return built;
    }

    const float extent_x = std::max(0.0f, max_x - min_x);
    const float extent_z = std::max(0.0f, max_z - min_z);
    const float target_cells = static_cast<float>(triangle_count);

    const auto axis_cells = [](const float extent, const float cell_size) {
        if (!(extent > 0.0f) || !(cell_size > 0.0f)) {
            return 1;
        }
        const float count = std::ceil(extent / cell_size);
        if (!(count > 1.0f)) {
            return 1;
        }
        if (!(count < static_cast<float>(max_index_cells_per_axis))) {
            return max_index_cells_per_axis;
        }
        return static_cast<int>(count);
    };

    int cells_x = 1;
    int cells_z = 1;
    if (extent_x > 0.0f && extent_z > 0.0f) {
        const float cell_size = std::sqrt((extent_x * extent_z) / target_cells);
        cells_x = axis_cells(extent_x, cell_size);
        cells_z = axis_cells(extent_z, cell_size);
    } else if (extent_x > 0.0f) {
        cells_x = axis_cells(extent_x, extent_x / std::min(target_cells, static_cast<float>(max_index_cells_per_axis)));
    } else if (extent_z > 0.0f) {
        cells_z = axis_cells(extent_z, extent_z / std::min(target_cells, static_cast<float>(max_index_cells_per_axis)));
    }

    built.min_x = min_x;
    built.min_z = min_z;
    built.max_x = max_x;
    built.max_z = max_z;
    built.cells_x = cells_x;
    built.cells_z = cells_z;
    built.cell_size_x = extent_x / static_cast<float>(cells_x);
    built.cell_size_z = extent_z / static_cast<float>(cells_z);

    const std::size_t cell_count = static_cast<std::size_t>(cells_x) * static_cast<std::size_t>(cells_z);
    std::vector<std::uint32_t> counts(cell_count, 0U);
    std::size_t total_entries = 0U;
    for (std::size_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const cell_range cells = triangle_cells(mesh, built, static_cast<int>(triangle));
        for (int cz = cells.z0; cz <= cells.z1; ++cz) {
            for (int cx = cells.x0; cx <= cells.x1; ++cx) {
                ++counts[cell_slot(built, cx, cz)];
                ++total_entries;
            }
        }
        if (total_entries > max_index_entries) {
            return terrain_mesh_index{};
        }
    }

    built.cell_starts.assign(cell_count + 1U, 0U);
    std::uint32_t running = 0U;
    for (std::size_t cell = 0U; cell < cell_count; ++cell) {
        built.cell_starts[cell] = running;
        running += counts[cell];
    }
    built.cell_starts[cell_count] = running;

    std::vector<std::uint32_t> cursors(built.cell_starts.begin(), built.cell_starts.end() - 1);
    built.cell_triangles.assign(static_cast<std::size_t>(running), 0U);
    for (std::size_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const cell_range cells = triangle_cells(mesh, built, static_cast<int>(triangle));
        for (int cz = cells.z0; cz <= cells.z1; ++cz) {
            for (int cx = cells.x0; cx <= cells.x1; ++cx) {
                const std::size_t cell = cell_slot(built, cx, cz);
                built.cell_triangles[cursors[cell]] = static_cast<std::uint32_t>(triangle);
                ++cursors[cell];
            }
        }
    }

    built.vertex_count = static_cast<std::uint32_t>(vertex_count);
    built.triangle_count = static_cast<std::uint32_t>(triangle_count);
    built.fingerprint_first = mesh.vertices.front().position;
    built.fingerprint_middle = mesh.vertices[vertex_count / 2U].position;
    built.fingerprint_last = mesh.vertices.back().position;
    return built;
}

bool terrain_index_matches(const terrain_mesh& mesh, const terrain_mesh_index& index) {
    if (index.cells_x <= 0 || index.cells_z <= 0 || mesh.vertices.empty()) {
        return false;
    }
    const std::size_t cell_count = static_cast<std::size_t>(index.cells_x) * static_cast<std::size_t>(index.cells_z);
    return index.cell_starts.size() == cell_count + 1U
        && static_cast<std::size_t>(index.vertex_count) == mesh.vertices.size()
        && static_cast<std::size_t>(index.triangle_count) * 3U == mesh.indices.size()
        && static_cast<std::size_t>(index.cell_starts.back()) == index.cell_triangles.size()
        && index.fingerprint_first == mesh.vertices.front().position
        && index.fingerprint_middle == mesh.vertices[mesh.vertices.size() / 2U].position
        && index.fingerprint_last == mesh.vertices.back().position;
}

// Squared distance to the nearest edge point; exactly what the edge
// candidates of scan_triangle compute, used to prune the ring search.
float triangle_edge_distance_squared(const terrain_mesh& mesh,
                                     const int triangle_index,
                                     const glm::vec3& position,
                                     const glm::vec3& query_point) {
    const glm::vec3& a = triangle_vertex(mesh, triangle_index, 0U).position;
    const glm::vec3& b = triangle_vertex(mesh, triangle_index, 1U).position;
    const glm::vec3& c = triangle_vertex(mesh, triangle_index, 2U).position;

    float best = std::numeric_limits<float>::max();
    for (int edge = 0; edge < 3; ++edge) {
        const glm::vec3 barycentric = closest_barycentric_on_edge(position, a, b, c, edge);
        const glm::vec3 point = a * barycentric.x + b * barycentric.y + c * barycentric.z;
        best = std::min(best, distance_squared(query_point, point));
    }
    return best;
}

// The best candidates found so far. Every traversal (full scan, indexed cell,
// ring search) feeds triangles through scan_triangle in ascending order, so
// they all pick the same winner.
struct terrain_scan_state {
    terrain_candidate best_inside;
    terrain_candidate best_edge;
    bool has_inside = false;
    bool has_edge = false;
    int triangles_tested = 0;
};

enum class scan_pass {
    inside_only,
    edge_only,
    both
};

terrain_scan_state scan_triangle(const terrain_mesh& mesh,
                                 const glm::vec3& position,
                                 const glm::vec3& query_point,
                                 const float fallback_y,
                                 const int preferred_row,
                                 const int triangle_index,
                                 const scan_pass pass,
                                 const terrain_scan_state& state) {
    terrain_scan_state next = state;
    ++next.triangles_tested;

    const terrain_vertex& a = triangle_vertex(mesh, triangle_index, 0U);
    const terrain_vertex& b = triangle_vertex(mesh, triangle_index, 1U);
    const terrain_vertex& c = triangle_vertex(mesh, triangle_index, 2U);
    const int row = triangle_row(mesh, triangle_index);
    const float section_distance = distance_xz_squared(query_point, triangle_section_center(mesh, triangle_index));

    const std::optional<glm::vec3> barycentric = barycentric_xz(position, a.position, b.position, c.position);
    if (barycentric) {
        if (pass == scan_pass::edge_only) {
            return next;
        }
        terrain_candidate candidate;
        candidate.sample = sample_from_barycentric(mesh, triangle_index, *barycentric, true);
        candidate.sample.point.x = position.x;
        candidate.sample.point.z = position.z;
        candidate.row = row;
        candidate.section_distance = section_distance;
        candidate.height_distance = std::abs(candidate.sample.point.y - fallback_y);
        if (!next.has_inside || is_better_inside_candidate(candidate, next.best_inside, preferred_row)) {
            next.best_inside = candidate;
            next.has_inside = true;
        }
        return next;
    }

    if (pass == scan_pass::inside_only) {
        return next;
    }

    for (int edge = 0; edge < 3; ++edge) {
        const glm::vec3 edge_barycentric = closest_barycentric_on_edge(position, a.position, b.position, c.position, edge);
        terrain_candidate candidate;
        candidate.sample = sample_from_barycentric(mesh, triangle_index, edge_barycentric, false);
        candidate.edge_distance = distance_squared(query_point, candidate.sample.point);
        candidate.sample.distance_from_center += std::sqrt(candidate.edge_distance);
        candidate.row = row;
        candidate.section_distance = section_distance;
        if (!next.has_edge || is_better_edge_candidate(candidate, next.best_edge, preferred_row)) {
            next.best_edge = candidate;
            next.has_edge = true;
        }
    }
    return next;
}

terrain_sample finish_terrain_scan(const terrain_scan_state& state, const glm::vec3& query_point) {
    terrain_sample sample;
    if (state.has_inside) {
        sample = state.best_inside.sample;
    } else if (state.has_edge) {
        sample = state.best_edge.sample;
    } else {
        sample.point = query_point;
    }
    sample.triangles_tested = state.triangles_tested;
    return sample;
}

// Lower bound on the squared distance from the query point to anything the ring
// search has not visited yet. Every triangle outside the visited cell block has
// its padded bounding box outside that block, so its closest edge point cannot
// be nearer than the closest unvisited part of the grid.
float unvisited_lower_bound(const terrain_mesh_index& index, const cell_range& visited, const float x, const float z) {
    const float block_min_x = visited.x0 <= 0 ? index.min_x : index.min_x + index.cell_size_x * static_cast<float>(visited.x0);
    const float block_max_x = visited.x1 >= index.cells_x - 1 ? index.max_x : index.min_x + index.cell_size_x * static_cast<float>(visited.x1 + 1);
    const float block_min_z = visited.z0 <= 0 ? index.min_z : index.min_z + index.cell_size_z * static_cast<float>(visited.z0);
    const float block_max_z = visited.z1 >= index.cells_z - 1 ? index.max_z : index.min_z + index.cell_size_z * static_cast<float>(visited.z1 + 1);

    float best = std::numeric_limits<float>::max();
    if (visited.x0 > 0) {
        best = std::min(best, rect_distance_xz_squared(x, z, index.min_x, index.min_z, block_min_x, index.max_z));
    }
    if (visited.x1 < index.cells_x - 1) {
        best = std::min(best, rect_distance_xz_squared(x, z, block_max_x, index.min_z, index.max_x, index.max_z));
    }
    if (visited.z0 > 0) {
        best = std::min(best, rect_distance_xz_squared(x, z, block_min_x, index.min_z, block_max_x, block_min_z));
    }
    if (visited.z1 < index.cells_z - 1) {
        best = std::min(best, rect_distance_xz_squared(x, z, block_min_x, block_max_z, block_max_x, index.max_z));
    }
    return best;
}

}

// Two triangles per grid cell over `rows` x `columns` vertices laid out row by
// row starting at `first_vertex`.
std::vector<std::uint32_t> grid_triangle_indices(const int rows, const int columns, const std::uint32_t first_vertex) {
    std::vector<std::uint32_t> indices;
    if (rows < 2 || columns < 2) {
        return indices;
    }
    indices.reserve(static_cast<std::size_t>((rows - 1) * (columns - 1) * 6));
    for (int row = 0; row < rows - 1; ++row) {
        for (int column = 0; column < columns - 1; ++column) {
            const std::uint32_t a = first_vertex + static_cast<std::uint32_t>(row * columns + column);
            const std::uint32_t b = first_vertex + static_cast<std::uint32_t>((row + 1) * columns + column);
            const std::uint32_t c = a + 1U;
            const std::uint32_t d = b + 1U;
            indices.insert(indices.end(), {a, b, c, c, b, d});
        }
    }
    return indices;
}

// Area-weighted upward vertex normals from the triangles that use each vertex.
std::vector<terrain_vertex> with_smooth_normals(std::vector<terrain_vertex> vertices,
                                                const std::vector<std::uint32_t>& indices) {
    for (terrain_vertex& vertex : vertices) {
        vertex.normal = glm::vec3(0.0f);
    }
    for (std::size_t i = 0; i + 2U < indices.size(); i += 3U) {
        const std::uint32_t ia = indices[i];
        const std::uint32_t ib = indices[i + 1U];
        const std::uint32_t ic = indices[i + 2U];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
            continue;
        }
        glm::vec3 normal = triangle_normal(vertices[ia].position, vertices[ib].position, vertices[ic].position);
        if (normal.y < 0.0f) {
            normal = -normal;
        }
        vertices[ia].normal += normal;
        vertices[ib].normal += normal;
        vertices[ic].normal += normal;
    }
    for (terrain_vertex& vertex : vertices) {
        vertex.normal = ground_normal(vertex.normal);
        if (vertex.normal.y < 0.0f) {
            vertex.normal = -vertex.normal;
        }
    }
    return vertices;
}

glm::vec3 sample_terrain_spline_point(const terrain_spline& terrain, const float t) {
    const std::vector<glm::vec3>& points = terrain.control_points;
    if (points.empty()) {
        return glm::vec3(0.0f);
    }
    if (points.size() == 1) {
        return points.front();
    }

    const int last = static_cast<int>(points.size()) - 1;
    const float scaled = clamp01(t) * static_cast<float>(last);
    const int segment = std::min(static_cast<int>(std::floor(scaled)), last - 1);
    const float local_t = scaled - static_cast<float>(segment);
    const auto point = [&points](const int i) { return points[static_cast<std::size_t>(i)]; };
    return catmull_rom(point(std::max(segment - 1, 0)),
                       point(segment),
                       point(segment + 1),
                       point(std::min(segment + 2, last)),
                       local_t);
}

terrain_mesh build_terrain_mesh(const terrain_spline& terrain,
                                const std::vector<material_zone>& zones,
                                const terrain_zone_tuning& tuning) {
    terrain_mesh mesh;
    if (terrain.control_points.size() < 2 || terrain.width <= 0.00001f) {
        return mesh;
    }

    const float fairway_width = terrain.fairway_width > 0.00001f
        ? std::min(terrain.fairway_width, terrain.width)
        : terrain.width;
    const float fairway_half_width = fairway_width * 0.5f;

    const terrain_section_layout layout = make_section_layout(terrain);
    mesh.section_count = layout.base_sections + layout.cap_sections * 2;
    mesh.cross_section_count = ribbon_cross_section_count;
    mesh.width = terrain.width;

    std::vector<terrain_vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(mesh.section_count * mesh.cross_section_count));
    for (int section = 0; section < mesh.section_count; ++section) {
        const terrain_section_frame frame = terrain_frame_at_section(terrain, layout, section);
        for (int column = 0; column < mesh.cross_section_count; ++column) {
            const float offset = cross_section_offset(column, mesh.cross_section_count, terrain.width);
            terrain_vertex vertex;
            vertex.position = frame.center + frame.lateral * offset;
            vertex.position.y += frame.bank * offset + cross_section_height_offset(offset, terrain.width);
            vertex.distance_from_center = offset;
            vertex.material = std::abs(offset) <= fairway_half_width ? terrain_material::fairway : terrain_material::rough;
            if (const std::optional<zone_hit> hit = query_zone_hit(vertex.position, zones)) {
                vertex.position.y += zone_height_offset(*hit, tuning);
            }
            vertices.push_back(vertex);
        }
    }

    mesh.indices = grid_triangle_indices(mesh.section_count, mesh.cross_section_count, 0U);
    mesh.vertices = with_smooth_normals(std::move(vertices), mesh.indices);
    mesh.spatial_index = make_terrain_mesh_index(mesh);
    return mesh;
}

std::optional<terrain_material> zone_material_at(const std::vector<material_zone>& zones, const glm::vec3& position) {
    const std::optional<zone_hit> hit = query_zone_hit(position, zones);
    return hit ? std::optional<terrain_material>(hit->material) : std::nullopt;
}

terrain_mesh build_material_overlay_mesh(const terrain_mesh& ground,
                                         const std::vector<material_zone>& zones,
                                         const float lift,
                                         const float spacing) {
    terrain_mesh mesh;
    if (ground.vertices.empty() || ground.indices.size() < 3U || zones.empty()) {
        return mesh;
    }
    mesh.width = ground.width;
    const float step = std::max(0.1f, spacing);
    const auto edge_count = [step](const float length) {
        return std::max(1, static_cast<int>(std::ceil(length / step)));
    };

    // Each vertex shows whichever zone wins at its spot, so where zones
    // overlap every shape there draws the same colour.
    const auto draped_vertex = [&](const glm::vec3& point, const terrain_material material) {
        const terrain_sample sample = sample_terrain_anchor(ground, point, point.y);
        terrain_vertex vertex;
        vertex.position = sample.point + glm::vec3(0.0f, lift, 0.0f);
        vertex.normal = sample.normal;
        vertex.material = zone_material_at(zones, point).value_or(material);
        return vertex;
    };

    for (const material_zone& zone : zones) {
        const std::optional<terrain_material> material = material_from_zone(zone.type);
        if (!material) {
            continue;
        }

        const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
        if (zone.has_radius && zone.radius > 0.00001f) {
            // Rings around a centre vertex, at most `spacing` apart in both
            // directions, so the overlay bends with the ground under it.
            const int rings = edge_count(zone.radius);
            const int segments = std::max(overlay_circle_segments, edge_count(glm::two_pi<float>() * zone.radius));
            mesh.vertices.push_back(draped_vertex(zone.center, *material));
            for (int ring = 1; ring <= rings; ++ring) {
                const float radius = zone.radius * static_cast<float>(ring) / static_cast<float>(rings);
                for (int i = 0; i < segments; ++i) {
                    const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
                    const glm::vec3 point(zone.center.x + std::cos(angle) * radius, zone.center.y, zone.center.z + std::sin(angle) * radius);
                    mesh.vertices.push_back(draped_vertex(point, *material));
                }
            }
            const auto ring_vertex = [first, segments](const int ring, const int i) {
                return first + 1U + static_cast<std::uint32_t>((ring - 1) * segments + i % segments);
            };
            for (int i = 0; i < segments; ++i) {
                mesh.indices.insert(mesh.indices.end(), {first, ring_vertex(1, i), ring_vertex(1, i + 1)});
            }
            for (int ring = 1; ring < rings; ++ring) {
                for (int i = 0; i < segments; ++i) {
                    const std::uint32_t a = ring_vertex(ring, i);
                    const std::uint32_t b = ring_vertex(ring, i + 1);
                    const std::uint32_t c = ring_vertex(ring + 1, i);
                    const std::uint32_t d = ring_vertex(ring + 1, i + 1);
                    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
                }
            }
            continue;
        }

        if (zone.has_bounds) {
            const glm::vec3 low = glm::min(zone.bounds_min, zone.bounds_max);
            const glm::vec3 high = glm::max(zone.bounds_min, zone.bounds_max);
            const glm::vec3 center = (low + high) * 0.5f;
            const int columns = edge_count(high.x - low.x) + 1;
            const int rows = edge_count(high.z - low.z) + 1;
            for (int row = 0; row < rows; ++row) {
                const float v = static_cast<float>(row) / static_cast<float>(rows - 1);
                for (int column = 0; column < columns; ++column) {
                    const float u = static_cast<float>(column) / static_cast<float>(columns - 1);
                    const glm::vec3 local(low.x + (high.x - low.x) * u - center.x, 0.0f, low.z + (high.z - low.z) * v - center.z);
                    mesh.vertices.push_back(draped_vertex(center + rotate_about_y(local, zone.rotation), *material));
                }
            }
            const std::vector<std::uint32_t> grid = grid_triangle_indices(rows, columns, first);
            mesh.indices.insert(mesh.indices.end(), grid.begin(), grid.end());
        }
    }

    mesh.spatial_index = make_terrain_mesh_index(mesh);
    return mesh;
}

terrain_mesh build_terrain_mesh_index(terrain_mesh mesh) {
    mesh.spatial_index = make_terrain_mesh_index(mesh);
    return mesh;
}

terrain_sample sample_terrain_mesh(const terrain_mesh& mesh,
                                   const glm::vec3& position,
                                   const float fallback_y,
                                   const terrain_sample* previous_sample) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3U) {
        terrain_sample sample;
        sample.point = glm::vec3(position.x, fallback_y, position.z);
        return sample;
    }

    const int triangle_count = static_cast<int>(mesh.indices.size() / 3U);
    const glm::vec3 query_point(position.x, fallback_y, position.z);
    const int preferred_row = previous_sample != nullptr && previous_sample->triangle_index >= 0
        ? triangle_row(mesh, previous_sample->triangle_index)
        : -1;
    const auto scan = [&](const int triangle, const scan_pass pass, const terrain_scan_state& state) {
        return scan_triangle(mesh, position, query_point, fallback_y, preferred_row, triangle, pass, state);
    };

    const terrain_mesh_index& index = mesh.spatial_index;
    if (!terrain_index_matches(mesh, index)) {
        terrain_scan_state state;
        for (int triangle = 0; triangle < triangle_count; ++triangle) {
            state = scan(triangle, scan_pass::both, state);
        }
        return finish_terrain_scan(state, query_point);
    }

    // Pass 1: containing triangles. A triangle can only contain the query if
    // its padded box does, so every candidate lives in the query's cell. Cell
    // lists are in ascending triangle order, so the winner matches the full scan.
    terrain_scan_state state;
    const int cell_x = index_cell_coord(position.x, index.min_x, index.cell_size_x, index.cells_x);
    const int cell_z = index_cell_coord(position.z, index.min_z, index.cell_size_z, index.cells_z);
    const std::size_t cell = cell_slot(index, cell_x, cell_z);
    for (std::uint32_t slot = index.cell_starts[cell]; slot < index.cell_starts[cell + 1U]; ++slot) {
        state = scan(static_cast<int>(index.cell_triangles[slot]), scan_pass::inside_only, state);
    }
    if (state.has_inside) {
        return finish_terrain_scan(state, query_point);
    }

    // Pass 2: off the surface, so the nearest edge wins. Candidates on the
    // previous sample's row outrank all others, so when that row has
    // triangles the winner is among them.
    const int triangles_per_row = (mesh.cross_section_count - 1) * 2;
    if (preferred_row >= 0 && mesh.section_count > 0 && triangles_per_row > 0) {
        const int first = preferred_row * triangles_per_row;
        const int last = std::min(first + triangles_per_row, triangle_count);
        for (int triangle = first; triangle < last; ++triangle) {
            state = scan(triangle, scan_pass::edge_only, state);
        }
        if (state.has_edge) {
            return finish_terrain_scan(state, query_point);
        }
    }

    // Expanding-ring search over the grid. Stops once nothing unvisited can be
    // closer than the best edge distance so far; the gathered set is a
    // superset of every triangle that could win.
    std::vector<std::uint32_t> candidates;
    float best_distance = std::numeric_limits<float>::max();
    const int max_radius = std::max(index.cells_x, index.cells_z);
    for (int radius = 0; radius <= max_radius; ++radius) {
        cell_range ring;
        ring.x0 = std::max(0, cell_x - radius);
        ring.x1 = std::min(index.cells_x - 1, cell_x + radius);
        ring.z0 = std::max(0, cell_z - radius);
        ring.z1 = std::min(index.cells_z - 1, cell_z + radius);
        for (int cz = ring.z0; cz <= ring.z1; ++cz) {
            const bool interior_row = cz > cell_z - radius && cz < cell_z + radius;
            for (int cx = ring.x0; cx <= ring.x1; ++cx) {
                if (radius > 0 && interior_row && cx > cell_x - radius && cx < cell_x + radius) {
                    continue;
                }
                const std::size_t ring_cell = cell_slot(index, cx, cz);
                for (std::uint32_t slot = index.cell_starts[ring_cell]; slot < index.cell_starts[ring_cell + 1U]; ++slot) {
                    const std::uint32_t triangle = index.cell_triangles[slot];
                    candidates.push_back(triangle);
                    best_distance = std::min(best_distance,
                                             triangle_edge_distance_squared(mesh, static_cast<int>(triangle), position, query_point));
                }
            }
        }

        if (ring.x0 == 0 && ring.z0 == 0 && ring.x1 == index.cells_x - 1 && ring.z1 == index.cells_z - 1) {
            break;
        }
        if (unvisited_lower_bound(index, ring, position.x, position.z) > best_distance + index_prune_slack) {
            break;
        }
    }

    // Replay in ascending order, the order the full scan visits them in.
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    for (const std::uint32_t triangle : candidates) {
        state = scan(static_cast<int>(triangle), scan_pass::edge_only, state);
    }
    return finish_terrain_scan(state, query_point);
}

std::optional<terrain_sample> sample_terrain_inside(const terrain_mesh& mesh, const glm::vec3& position) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3U) {
        return std::nullopt;
    }
    const glm::vec3 query_point(position.x, 0.0f, position.z);
    terrain_scan_state state;
    const auto scan = [&](const int triangle) {
        state = scan_triangle(mesh, position, query_point, 0.0f, -1, triangle, scan_pass::inside_only, state);
    };
    const terrain_mesh_index& index = mesh.spatial_index;
    if (terrain_index_matches(mesh, index)) {
        const std::size_t cell = cell_slot(index,
                                           index_cell_coord(position.x, index.min_x, index.cell_size_x, index.cells_x),
                                           index_cell_coord(position.z, index.min_z, index.cell_size_z, index.cells_z));
        for (std::uint32_t slot = index.cell_starts[cell]; slot < index.cell_starts[cell + 1U]; ++slot) {
            scan(static_cast<int>(index.cell_triangles[slot]));
        }
    } else {
        for (int triangle = 0; triangle < static_cast<int>(mesh.indices.size() / 3U); ++triangle) {
            scan(triangle);
        }
    }
    if (!state.has_inside) {
        return std::nullopt;
    }
    return finish_terrain_scan(state, query_point);
}

terrain_sample sample_terrain_anchor(const terrain_mesh& mesh, const glm::vec3& position, const float fallback_y) {
    terrain_sample sample = sample_terrain_mesh(mesh, position, fallback_y);
    sample.point.x = position.x;
    sample.point.z = position.z;
    return sample;
}
