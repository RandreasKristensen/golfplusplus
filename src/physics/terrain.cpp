#include "physics/terrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
float clamp01(const float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float smoothstep(const float edge0, const float edge1, const float value) {
    const float t = clamp01((value - edge0) / std::max(0.00001f, edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float distance_xz_squared(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

float distance_squared(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

float control_polygon_xz_length(const std::vector<glm::vec3>& points) {
    float length = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const float dx = points[i].x - points[i - 1].x;
        const float dz = points[i].z - points[i - 1].z;
        length += std::sqrt(dx * dx + dz * dz);
    }
    return length;
}

glm::vec3 safe_normalize(const glm::vec3& value, const glm::vec3& fallback) {
    const float len = glm::length(value);
    if (len <= 0.00001f) {
        return fallback;
    }
    return value / len;
}

int material_priority(const terrain_material material) {
    switch (material) {
    case terrain_material::water:
        return 3;
    case terrain_material::bunker:
        return 2;
    case terrain_material::green:
        return 1;
    case terrain_material::fairway:
        return 0;
    case terrain_material::rough:
    default:
        return -1;
    }
}

int material_index(const terrain_material material) {
    switch (material) {
    case terrain_material::fairway:
        return 0;
    case terrain_material::rough:
        return 1;
    case terrain_material::green:
        return 2;
    case terrain_material::bunker:
        return 3;
    case terrain_material::water:
        return 4;
    default:
        return 0;
    }
}

terrain_material material_from_zone(const material_zone_type type) {
    switch (type) {
    case material_zone_type::green:
        return terrain_material::green;
    case material_zone_type::bunker:
        return terrain_material::bunker;
    case material_zone_type::water:
        return terrain_material::water;
    default:
        return terrain_material::fairway;
    }
}

int zone_priority(const material_zone_type type) {
    switch (type) {
    case material_zone_type::water:
        return 3;
    case material_zone_type::bunker:
        return 2;
    case material_zone_type::green:
        return 1;
    default:
        return 0;
    }
}

bool zone_contains(const material_zone& zone, const glm::vec3& position, float& out_normalized_distance) {
    if (zone.has_radius && zone.radius > 0.00001f) {
        const float dist = std::sqrt(distance_xz_squared(position, zone.center));
        if (dist > zone.radius) {
            return false;
        }
        out_normalized_distance = clamp01(dist / zone.radius);
        return true;
    }

    if (zone.has_bounds) {
        const float min_x = std::min(zone.bounds_min.x, zone.bounds_max.x);
        const float max_x = std::max(zone.bounds_min.x, zone.bounds_max.x);
        const float min_z = std::min(zone.bounds_min.z, zone.bounds_max.z);
        const float max_z = std::max(zone.bounds_min.z, zone.bounds_max.z);
        if (position.x < min_x || position.x > max_x || position.z < min_z || position.z > max_z) {
            return false;
        }

        const glm::vec3 center = (zone.bounds_min + zone.bounds_max) * 0.5f;
        const glm::vec3 half = glm::max(glm::abs(zone.bounds_max - zone.bounds_min) * 0.5f, glm::vec3(0.0001f));
        const float norm_x = std::abs(position.x - center.x) / half.x;
        const float norm_z = std::abs(position.z - center.z) / half.z;
        out_normalized_distance = clamp01(std::max(norm_x, norm_z));
        return true;
    }

    return false;
}

struct zone_hit {
    bool has_hit = false;
    terrain_material material = terrain_material::fairway;
    float normalized_distance = 1.0f;
};

zone_hit query_zone_hit(const glm::vec3& position, const std::vector<material_zone>& zones) {
    zone_hit hit;
    int best_priority = -1;
    float best_distance = 1.0f;

    for (const material_zone& zone : zones) {
        float normalized_distance = 0.0f;
        if (!zone_contains(zone, position, normalized_distance)) {
            continue;
        }

        const int priority = zone_priority(zone.type);
        if (priority < best_priority) {
            continue;
        }

        if (priority == best_priority && normalized_distance >= best_distance) {
            continue;
        }

        best_priority = priority;
        best_distance = normalized_distance;
        hit.has_hit = true;
        hit.material = material_from_zone(zone.type);
        hit.normalized_distance = normalized_distance;
    }

    return hit;
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

glm::vec3 terrain_tangent_at(const terrain_spline& terrain, const float t) {
    const float step = 1.0f / static_cast<float>(std::max(terrain.sample_count, 2) * 4);
    const glm::vec3 before = sample_terrain_spline_point(terrain, clamp01(t - step));
    const glm::vec3 after = sample_terrain_spline_point(terrain, clamp01(t + step));
    return safe_normalize(after - before, glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::vec3 terrain_lateral_at(const terrain_spline& terrain, const float t) {
    const glm::vec3 tangent = terrain_tangent_at(terrain, t);
    return safe_normalize(glm::vec3(-tangent.z, 0.0f, tangent.x), glm::vec3(1.0f, 0.0f, 0.0f));
}

int terrain_base_section_count(const terrain_spline& terrain) {
    const float length = control_polygon_xz_length(terrain.control_points);
    const int length_sections = static_cast<int>(std::ceil(length)) + 1;
    return std::max(2, std::max(terrain.sample_count, length_sections));
}

int terrain_cap_section_count(const terrain_spline& terrain) {
    const float extension = terrain.width * 0.5f;
    if (extension <= 0.00001f) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::ceil(extension)));
}

glm::vec3 endpoint_extension_tangent(const terrain_spline& terrain, const bool end) {
    if (terrain.control_points.size() < 2) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const glm::vec3 a = end
        ? terrain.control_points[terrain.control_points.size() - 2U]
        : terrain.control_points[0];
    const glm::vec3 b = end
        ? terrain.control_points.back()
        : terrain.control_points[1];
    const glm::vec3 delta = b - a;
    const float xz_length = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    if (xz_length <= 0.00001f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return delta / xz_length;
}

glm::vec3 lateral_from_tangent(const glm::vec3& tangent) {
    return safe_normalize(glm::vec3(-tangent.z, 0.0f, tangent.x), glm::vec3(1.0f, 0.0f, 0.0f));
}

struct terrain_section_layout {
    int base_sections = 0;
    int cap_sections = 0;
    float cap_extension = 0.0f;
};

terrain_section_layout make_section_layout(const terrain_spline& terrain) {
    terrain_section_layout layout;
    layout.base_sections = terrain_base_section_count(terrain);
    layout.cap_sections = terrain_cap_section_count(terrain);
    layout.cap_extension = terrain.width * 0.5f;
    return layout;
}

struct terrain_section_frame {
    glm::vec3 center{0.0f};
    glm::vec3 lateral{1.0f, 0.0f, 0.0f};
};

terrain_section_frame terrain_frame_at_section(const terrain_spline& terrain,
                                               const terrain_section_layout& layout,
                                               const int section) {
    terrain_section_frame frame;
    const glm::vec3 start_tangent = endpoint_extension_tangent(terrain, false);
    const glm::vec3 end_tangent = endpoint_extension_tangent(terrain, true);

    if (section < layout.cap_sections) {
        const float u = static_cast<float>(section) / static_cast<float>(std::max(1, layout.cap_sections));
        const float distance = -layout.cap_extension * (1.0f - u);
        frame.center = terrain.control_points.front() + start_tangent * distance;
        frame.lateral = lateral_from_tangent(start_tangent);
        return frame;
    }

    const int base_section = section - layout.cap_sections;
    if (base_section < layout.base_sections) {
        const float t = static_cast<float>(base_section) / static_cast<float>(std::max(1, layout.base_sections - 1));
        frame.center = sample_terrain_spline_point(terrain, t);
        frame.lateral = terrain_lateral_at(terrain, t);
        return frame;
    }

    const int after_section = base_section - layout.base_sections;
    const float u = static_cast<float>(after_section + 1) / static_cast<float>(std::max(1, layout.cap_sections));
    frame.center = terrain.control_points.back() + end_tangent * (layout.cap_extension * u);
    frame.lateral = lateral_from_tangent(end_tangent);
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
    const float edge = std::max(0.0f, normalized - 0.55f) / 0.45f;
    return -0.18f * edge * edge;
}

glm::vec3 triangle_normal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return safe_normalize(glm::cross(b - a, c - a), glm::vec3(0.0f, 1.0f, 0.0f));
}

void add_vertex_normal(std::vector<terrain_vertex>& vertices, const uint32_t index, const glm::vec3& normal) {
    if (index < vertices.size()) {
        vertices[index].normal += normal;
    }
}

float signed_area_xz(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

bool barycentric_xz(const glm::vec3& p,
                    const glm::vec3& a,
                    const glm::vec3& b,
                    const glm::vec3& c,
                    glm::vec3& out) {
    const float area = signed_area_xz(a, b, c);
    if (std::abs(area) <= 0.000001f) {
        return false;
    }

    const float w0 = signed_area_xz(p, b, c) / area;
    const float w1 = signed_area_xz(p, c, a) / area;
    const float w2 = 1.0f - w0 - w1;
    out = glm::vec3(w0, w1, w2);
    return w0 >= -0.0001f && w1 >= -0.0001f && w2 >= -0.0001f;
}

terrain_sample sample_from_barycentric(const terrain_mesh& mesh,
                                       const int triangle_index,
                                       const glm::vec3& barycentric,
                                       const bool inside_surface) {
    const std::size_t index = static_cast<std::size_t>(triangle_index) * 3U;
    const terrain_vertex& a = mesh.vertices[mesh.indices[index]];
    const terrain_vertex& b = mesh.vertices[mesh.indices[index + 1U]];
    const terrain_vertex& c = mesh.vertices[mesh.indices[index + 2U]];

    terrain_sample sample;
    sample.point = a.position * barycentric.x + b.position * barycentric.y + c.position * barycentric.z;
    sample.normal = safe_normalize(a.normal * barycentric.x + b.normal * barycentric.y + c.normal * barycentric.z,
                                   triangle_normal(a.position, b.position, c.position));
    sample.barycentric = barycentric;
    sample.distance_from_center = std::abs(a.distance_from_center * barycentric.x
        + b.distance_from_center * barycentric.y
        + c.distance_from_center * barycentric.z);
    sample.triangle_index = triangle_index;
    if (inside_surface) {
        constexpr int material_count = 5;
        std::array<float, material_count> weights{};
        const auto add_weight = [&](const terrain_material material, const float weight) {
            const int index = material_index(material);
            if (index < 0 || index >= material_count) {
                return;
            }
            weights[static_cast<std::size_t>(index)] += weight;
        };
        add_weight(a.material, barycentric.x);
        add_weight(b.material, barycentric.y);
        add_weight(c.material, barycentric.z);

        terrain_material best = terrain_material::fairway;
        float best_weight = -1.0f;
        int best_priority = -1;
        for (int i = 0; i < material_count; ++i) {
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

        sample.material = best;
    } else {
        sample.material = terrain_material::rough;
    }
    sample.has_spline = true;
    sample.inside_surface = inside_surface;
    return sample;
}

glm::vec3 closest_barycentric_on_edge(const glm::vec3& position,
                                      const glm::vec3& a,
                                      const glm::vec3& b,
                                      const glm::vec3& c,
                                      const int edge) {
    const glm::vec3 points[3] = {a, b, c};
    const int i0 = edge;
    const int i1 = (edge + 1) % 3;
    const glm::vec3 edge_start = points[i0];
    const glm::vec3 edge_end = points[i1];
    const glm::vec3 edge_xz(edge_end.x - edge_start.x, 0.0f, edge_end.z - edge_start.z);
    const glm::vec3 delta_xz(position.x - edge_start.x, 0.0f, position.z - edge_start.z);
    const float denom = glm::dot(edge_xz, edge_xz);
    const float t = denom > 0.000001f ? clamp01(glm::dot(delta_xz, edge_xz) / denom) : 0.0f;

    glm::vec3 barycentric(0.0f);
    barycentric[i0] = 1.0f - t;
    barycentric[i1] = t;
    return barycentric;
}

int triangle_section_index(const terrain_mesh& mesh, const int triangle_index) {
    if (mesh.section_count <= 0 || mesh.cross_section_count < 2) {
        return 0;
    }

    const int triangles_per_section = (mesh.cross_section_count - 1) * 2;
    if (triangles_per_section <= 0) {
        return 0;
    }

    return std::clamp(triangle_index / triangles_per_section, 0, mesh.section_count - 1);
}

glm::vec3 section_center_point(const terrain_mesh& mesh, const int section_index) {
    if (mesh.vertices.empty() || mesh.cross_section_count <= 0 || mesh.section_count <= 0) {
        return glm::vec3(0.0f);
    }

    const int clamped_section = std::clamp(section_index, 0, mesh.section_count - 1);
    const int center_column = mesh.cross_section_count / 2;
    const std::size_t index = static_cast<std::size_t>(clamped_section * mesh.cross_section_count + center_column);
    if (index >= mesh.vertices.size()) {
        return glm::vec3(0.0f);
    }

    return mesh.vertices[index].position;
}

struct terrain_candidate {
    terrain_sample sample;
    int section_index = 0;
    float section_distance = std::numeric_limits<float>::max();
    float height_distance = std::numeric_limits<float>::max();
    float edge_distance = std::numeric_limits<float>::max();
};

bool is_better_inside_candidate(const terrain_candidate& candidate,
                                const terrain_candidate& best,
                                const int preferred_section) {
    constexpr float epsilon = 0.00001f;
    const bool candidate_preferred = preferred_section >= 0 && candidate.section_index == preferred_section;
    const bool best_preferred = preferred_section >= 0 && best.section_index == preferred_section;
    if (candidate_preferred != best_preferred) {
        return candidate_preferred;
    }
    if (candidate.section_distance < best.section_distance - epsilon) {
        return true;
    }
    if (candidate.section_distance > best.section_distance + epsilon) {
        return false;
    }
    if (candidate.height_distance < best.height_distance - epsilon) {
        return true;
    }
    if (candidate.height_distance > best.height_distance + epsilon) {
        return false;
    }
    if (candidate.sample.distance_from_center < best.sample.distance_from_center - epsilon) {
        return true;
    }
    if (candidate.sample.distance_from_center > best.sample.distance_from_center + epsilon) {
        return false;
    }
    return candidate.sample.triangle_index < best.sample.triangle_index;
}

bool is_better_edge_candidate(const terrain_candidate& candidate,
                              const terrain_candidate& best,
                              const int preferred_section) {
    constexpr float epsilon = 0.00001f;
    const bool candidate_preferred = preferred_section >= 0 && candidate.section_index == preferred_section;
    const bool best_preferred = preferred_section >= 0 && best.section_index == preferred_section;
    if (candidate_preferred != best_preferred) {
        return candidate_preferred;
    }
    if (candidate.edge_distance < best.edge_distance - epsilon) {
        return true;
    }
    if (candidate.edge_distance > best.edge_distance + epsilon) {
        return false;
    }
    if (candidate.section_distance < best.section_distance - epsilon) {
        return true;
    }
    if (candidate.section_distance > best.section_distance + epsilon) {
        return false;
    }
    if (candidate.sample.distance_from_center < best.sample.distance_from_center - epsilon) {
        return true;
    }
    if (candidate.sample.distance_from_center > best.sample.distance_from_center + epsilon) {
        return false;
    }
    return candidate.sample.triangle_index < best.sample.triangle_index;
}

// ---------------------------------------------------------------------------
// Spatial index (uniform XZ grid, CSR storage)
// ---------------------------------------------------------------------------

constexpr int max_index_cells_per_axis = 256;
constexpr std::size_t max_index_entries = 8000000U;
// Slack used when pruning the ring search. Must be >= the epsilon the candidate
// comparators use so ties are never pruned away.
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
    const std::size_t base = static_cast<std::size_t>(triangle_index) * 3U;
    const glm::vec3& a = mesh.vertices[mesh.indices[base]].position;
    const glm::vec3& b = mesh.vertices[mesh.indices[base + 1U]].position;
    const glm::vec3& c = mesh.vertices[mesh.indices[base + 2U]].position;

    triangle_xz_bounds bounds;
    bounds.min_x = std::min(a.x, std::min(b.x, c.x));
    bounds.max_x = std::max(a.x, std::max(b.x, c.x));
    bounds.min_z = std::min(a.z, std::min(b.z, c.z));
    bounds.max_z = std::max(a.z, std::max(b.z, c.z));

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

terrain_mesh_index make_terrain_mesh_index(const terrain_mesh& mesh) {
    terrain_mesh_index built;
    const std::size_t triangle_count = mesh.indices.size() / 3U;
    const std::size_t vertex_count = mesh.vertices.size();
    if (vertex_count == 0U || triangle_count == 0U) {
        return built;
    }
    if (triangle_count * 3U != mesh.indices.size()) {
        return built;
    }
    if (triangle_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return built;
    }
    for (const uint32_t index : mesh.indices) {
        if (static_cast<std::size_t>(index) >= vertex_count) {
            return terrain_mesh_index{};
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
        return terrain_mesh_index{};
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
    std::vector<uint32_t> counts(cell_count, 0U);
    std::size_t total_entries = 0U;
    for (std::size_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const triangle_xz_bounds bounds = padded_triangle_bounds(mesh, static_cast<int>(triangle));
        const int x0 = index_cell_coord(bounds.min_x, min_x, built.cell_size_x, cells_x);
        const int x1 = index_cell_coord(bounds.max_x, min_x, built.cell_size_x, cells_x);
        const int z0 = index_cell_coord(bounds.min_z, min_z, built.cell_size_z, cells_z);
        const int z1 = index_cell_coord(bounds.max_z, min_z, built.cell_size_z, cells_z);
        for (int cz = z0; cz <= z1; ++cz) {
            for (int cx = x0; cx <= x1; ++cx) {
                ++counts[static_cast<std::size_t>(cz) * static_cast<std::size_t>(cells_x) + static_cast<std::size_t>(cx)];
                ++total_entries;
            }
        }
        if (total_entries > max_index_entries) {
            return terrain_mesh_index{};
        }
    }

    built.cell_starts.assign(cell_count + 1U, 0U);
    uint32_t running = 0U;
    for (std::size_t cell = 0U; cell < cell_count; ++cell) {
        built.cell_starts[cell] = running;
        running += counts[cell];
    }
    built.cell_starts[cell_count] = running;

    std::vector<uint32_t> cursors(built.cell_starts.begin(), built.cell_starts.end() - 1);
    built.cell_triangles.assign(static_cast<std::size_t>(running), 0U);
    for (std::size_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const triangle_xz_bounds bounds = padded_triangle_bounds(mesh, static_cast<int>(triangle));
        const int x0 = index_cell_coord(bounds.min_x, min_x, built.cell_size_x, cells_x);
        const int x1 = index_cell_coord(bounds.max_x, min_x, built.cell_size_x, cells_x);
        const int z0 = index_cell_coord(bounds.min_z, min_z, built.cell_size_z, cells_z);
        const int z1 = index_cell_coord(bounds.max_z, min_z, built.cell_size_z, cells_z);
        for (int cz = z0; cz <= z1; ++cz) {
            for (int cx = x0; cx <= x1; ++cx) {
                const std::size_t cell = static_cast<std::size_t>(cz) * static_cast<std::size_t>(cells_x) + static_cast<std::size_t>(cx);
                built.cell_triangles[cursors[cell]] = static_cast<uint32_t>(triangle);
                ++cursors[cell];
            }
        }
    }

    built.vertex_count = static_cast<uint32_t>(vertex_count);
    built.triangle_count = static_cast<uint32_t>(triangle_count);
    built.fingerprint_first = mesh.vertices.front().position;
    built.fingerprint_middle = mesh.vertices[vertex_count / 2U].position;
    built.fingerprint_last = mesh.vertices.back().position;
    return built;
}

bool same_position(const glm::vec3& a, const glm::vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool terrain_index_matches(const terrain_mesh& mesh, const terrain_mesh_index& index) {
    if (index.cells_x <= 0 || index.cells_z <= 0) {
        return false;
    }
    const std::size_t cell_count = static_cast<std::size_t>(index.cells_x) * static_cast<std::size_t>(index.cells_z);
    if (index.cell_starts.size() != cell_count + 1U) {
        return false;
    }
    if (mesh.vertices.empty()) {
        return false;
    }
    if (static_cast<std::size_t>(index.vertex_count) != mesh.vertices.size()) {
        return false;
    }
    if (static_cast<std::size_t>(index.triangle_count) * 3U != mesh.indices.size()) {
        return false;
    }
    if (static_cast<std::size_t>(index.cell_starts.back()) != index.cell_triangles.size()) {
        return false;
    }
    return same_position(index.fingerprint_first, mesh.vertices.front().position)
        && same_position(index.fingerprint_middle, mesh.vertices[mesh.vertices.size() / 2U].position)
        && same_position(index.fingerprint_last, mesh.vertices.back().position);
}

// Cheap version of the edge candidate distance, used only to prune the ring
// search. Produces exactly the distance the full candidate evaluation would.
float triangle_edge_distance_squared(const terrain_mesh& mesh,
                                     const int triangle_index,
                                     const glm::vec3& position,
                                     const glm::vec3& query_point) {
    const std::size_t base = static_cast<std::size_t>(triangle_index) * 3U;
    const glm::vec3& a = mesh.vertices[mesh.indices[base]].position;
    const glm::vec3& b = mesh.vertices[mesh.indices[base + 1U]].position;
    const glm::vec3& c = mesh.vertices[mesh.indices[base + 2U]].position;

    float best = std::numeric_limits<float>::max();
    for (int edge = 0; edge < 3; ++edge) {
        const glm::vec3 barycentric = closest_barycentric_on_edge(position, a, b, c, edge);
        const glm::vec3 point = a * barycentric.x + b * barycentric.y + c * barycentric.z;
        best = std::min(best, distance_squared(query_point, point));
    }
    return best;
}

// ---------------------------------------------------------------------------
// Scan state — accumulates the same "best candidate" decision the original full
// scan made, but as a pure value so the traversal order can be narrowed down.
// ---------------------------------------------------------------------------

struct terrain_scan_state {
    terrain_candidate best_inside;
    terrain_candidate best_edge;
    bool has_inside = false;
    bool has_edge = false;
    int triangles_tested = 0;
};

terrain_scan_state scan_triangle(const terrain_mesh& mesh,
                                 const glm::vec3& position,
                                 const glm::vec3& query_point,
                                 const float fallback_y,
                                 const int preferred_section,
                                 const int triangle_index,
                                 const bool collect_inside,
                                 const bool collect_edge,
                                 const terrain_scan_state state) {
    terrain_scan_state next = state;
    ++next.triangles_tested;

    const std::size_t base = static_cast<std::size_t>(triangle_index) * 3U;
    const terrain_vertex& a = mesh.vertices[mesh.indices[base]];
    const terrain_vertex& b = mesh.vertices[mesh.indices[base + 1U]];
    const terrain_vertex& c = mesh.vertices[mesh.indices[base + 2U]];

    glm::vec3 barycentric(0.0f);
    if (barycentric_xz(position, a.position, b.position, c.position, barycentric)) {
        if (!collect_inside) {
            return next;
        }
        const int section_index = triangle_section_index(mesh, triangle_index);
        terrain_sample sample = sample_from_barycentric(mesh, triangle_index, barycentric, true);
        sample.point.x = position.x;
        sample.point.z = position.z;
        terrain_candidate candidate;
        candidate.sample = sample;
        candidate.section_index = section_index;
        candidate.section_distance = distance_xz_squared(query_point, section_center_point(mesh, section_index));
        candidate.height_distance = std::abs(sample.point.y - fallback_y);
        if (!next.has_inside || is_better_inside_candidate(candidate, next.best_inside, preferred_section)) {
            next.best_inside = candidate;
            next.has_inside = true;
        }
        return next;
    }

    if (!collect_edge) {
        return next;
    }

    const int section_index = triangle_section_index(mesh, triangle_index);
    const float section_distance = distance_xz_squared(query_point, section_center_point(mesh, section_index));
    for (int edge = 0; edge < 3; ++edge) {
        const glm::vec3 edge_barycentric = closest_barycentric_on_edge(position, a.position, b.position, c.position, edge);
        terrain_sample edge_sample = sample_from_barycentric(mesh, triangle_index, edge_barycentric, false);
        const float distance = distance_squared(query_point, edge_sample.point);
        edge_sample.distance_from_center += std::sqrt(distance);
        terrain_candidate candidate;
        candidate.sample = edge_sample;
        candidate.section_index = section_index;
        candidate.section_distance = section_distance;
        candidate.edge_distance = distance;
        if (!next.has_edge || is_better_edge_candidate(candidate, next.best_edge, preferred_section)) {
            next.best_edge = candidate;
            next.has_edge = true;
        }
    }
    return next;
}

terrain_sample finish_terrain_scan(const terrain_scan_state& state, const glm::vec3& query_point) {
    if (state.has_inside) {
        terrain_sample sample = state.best_inside.sample;
        sample.triangles_tested = state.triangles_tested;
        return sample;
    }
    if (state.has_edge) {
        terrain_sample sample = state.best_edge.sample;
        sample.has_spline = true;
        sample.inside_surface = false;
        sample.material = terrain_material::rough;
        sample.triangles_tested = state.triangles_tested;
        return sample;
    }
    terrain_sample sample;
    sample.point = query_point;
    sample.triangles_tested = state.triangles_tested;
    return sample;
}

// Lower bound on the squared distance from the query point to anything the ring
// search has not visited yet. Every triangle outside the visited cell block has
// its padded bounding box outside that block, so its closest edge point cannot
// be nearer than the closest unvisited part of the grid.
float unvisited_lower_bound(const terrain_mesh_index& index,
                            const int x0,
                            const int x1,
                            const int z0,
                            const int z1,
                            const float x,
                            const float z) {
    const float block_min_x = x0 <= 0 ? index.min_x : index.min_x + index.cell_size_x * static_cast<float>(x0);
    const float block_max_x = x1 >= index.cells_x - 1 ? index.max_x : index.min_x + index.cell_size_x * static_cast<float>(x1 + 1);
    const float block_min_z = z0 <= 0 ? index.min_z : index.min_z + index.cell_size_z * static_cast<float>(z0);
    const float block_max_z = z1 >= index.cells_z - 1 ? index.max_z : index.min_z + index.cell_size_z * static_cast<float>(z1 + 1);

    float best = std::numeric_limits<float>::max();
    if (x0 > 0) {
        best = std::min(best, rect_distance_xz_squared(x, z, index.min_x, index.min_z, block_min_x, index.max_z));
    }
    if (x1 < index.cells_x - 1) {
        best = std::min(best, rect_distance_xz_squared(x, z, block_max_x, index.min_z, index.max_x, index.max_z));
    }
    if (z0 > 0) {
        best = std::min(best, rect_distance_xz_squared(x, z, block_min_x, index.min_z, block_max_x, block_min_z));
    }
    if (z1 < index.cells_z - 1) {
        best = std::min(best, rect_distance_xz_squared(x, z, block_min_x, block_max_z, block_max_x, index.max_z));
    }
    return best;
}
}

glm::vec3 sample_terrain_spline_point(const terrain_spline& terrain, const float t) {
    if (terrain.control_points.empty()) {
        return glm::vec3(0.0f);
    }

    if (terrain.control_points.size() == 1) {
        return terrain.control_points.front();
    }

    const float clamped_t = clamp01(t);
    const float scaled = clamped_t * static_cast<float>(terrain.control_points.size() - 1);
    const int segment = std::min(static_cast<int>(std::floor(scaled)),
                                 static_cast<int>(terrain.control_points.size() - 2));
    const float local_t = scaled - static_cast<float>(segment);

    const int p0 = std::max(segment - 1, 0);
    const int p1 = segment;
    const int p2 = segment + 1;
    const int p3 = std::min(segment + 2, static_cast<int>(terrain.control_points.size() - 1));

    return catmull_rom(terrain.control_points[p0],
                       terrain.control_points[p1],
                       terrain.control_points[p2],
                       terrain.control_points[p3],
                       local_t);
}

terrain_mesh build_terrain_mesh(const terrain_spline& terrain) {
    return build_terrain_mesh(terrain, {}, terrain_zone_tuning{});
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
    mesh.cross_section_count = 9;
    mesh.width = terrain.width;
    mesh.vertices.reserve(static_cast<std::size_t>(mesh.section_count * mesh.cross_section_count));
    mesh.indices.reserve(static_cast<std::size_t>((mesh.section_count - 1) * (mesh.cross_section_count - 1) * 6));

    for (int section = 0; section < mesh.section_count; ++section) {
        const terrain_section_frame frame = terrain_frame_at_section(terrain, layout, section);

        for (int column = 0; column < mesh.cross_section_count; ++column) {
            const float offset = cross_section_offset(column, mesh.cross_section_count, terrain.width);
            terrain_vertex vertex;
            vertex.position = frame.center + frame.lateral * offset;
            vertex.position.y += cross_section_height_offset(offset, terrain.width);
            vertex.normal = glm::vec3(0.0f);
            vertex.distance_from_center = offset;
            vertex.material = std::abs(offset) <= fairway_half_width
                ? terrain_material::fairway
                : terrain_material::rough;

            const zone_hit hit = query_zone_hit(vertex.position, zones);
            if (hit.has_hit) {
                vertex.material = hit.material;
                if (hit.material == terrain_material::bunker && tuning.bunker_depth > 0.0f) {
                    const float t = clamp01(hit.normalized_distance);
                    const float bowl = -(1.0f - t * t) * tuning.bunker_depth;
                    vertex.position.y += bowl;
                } else if (hit.material == terrain_material::water && tuning.water_depth > 0.0f) {
                    constexpr float edge_softness = 0.25f;
                    const float t = clamp01(hit.normalized_distance);
                    const float edge = smoothstep(1.0f - edge_softness, 1.0f, t);
                    const float depth = -tuning.water_depth * (1.0f - edge);
                    vertex.position.y += depth;
                }
            }
            mesh.vertices.push_back(vertex);
        }
    }

    for (int section = 0; section < mesh.section_count - 1; ++section) {
        for (int column = 0; column < mesh.cross_section_count - 1; ++column) {
            const uint32_t a = static_cast<uint32_t>(section * mesh.cross_section_count + column);
            const uint32_t b = static_cast<uint32_t>((section + 1) * mesh.cross_section_count + column);
            const uint32_t c = static_cast<uint32_t>(section * mesh.cross_section_count + column + 1);
            const uint32_t d = static_cast<uint32_t>((section + 1) * mesh.cross_section_count + column + 1);
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
        }
    }

    for (std::size_t i = 0; i + 2U < mesh.indices.size(); i += 3U) {
        const uint32_t ia = mesh.indices[i];
        const uint32_t ib = mesh.indices[i + 1U];
        const uint32_t ic = mesh.indices[i + 2U];
        glm::vec3 normal = triangle_normal(mesh.vertices[ia].position, mesh.vertices[ib].position, mesh.vertices[ic].position);
        if (normal.y < 0.0f) {
            normal = -normal;
        }
        add_vertex_normal(mesh.vertices, ia, normal);
        add_vertex_normal(mesh.vertices, ib, normal);
        add_vertex_normal(mesh.vertices, ic, normal);
    }

    for (terrain_vertex& vertex : mesh.vertices) {
        vertex.normal = safe_normalize(vertex.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        if (vertex.normal.y < 0.0f) {
            vertex.normal = -vertex.normal;
        }
    }

    mesh.spatial_index = make_terrain_mesh_index(mesh);
    return mesh;
}

terrain_mesh build_material_overlay_mesh(const terrain_mesh& source_mesh,
                                         const std::vector<material_zone>& zones,
                                         const float lift) {
    terrain_mesh mesh;
    if (source_mesh.vertices.empty() || source_mesh.indices.size() < 3U || zones.empty()) {
        return mesh;
    }

    mesh.width = source_mesh.width;

    const auto append_sampled_vertex = [&](const glm::vec3& authored_position,
                                           const terrain_material material) {
        const terrain_sample sample = sample_terrain_anchor(source_mesh, authored_position, authored_position.y);
        terrain_vertex vertex;
        vertex.position = sample.point + glm::vec3(0.0f, lift, 0.0f);
        vertex.normal = sample.normal;
        vertex.distance_from_center = sample.distance_from_center;
        vertex.material = material;
        mesh.vertices.push_back(vertex);
        return static_cast<uint32_t>(mesh.vertices.size() - 1U);
    };

    constexpr int radius_segments = 32;
    constexpr int bounds_resolution = 6;

    for (const material_zone& zone : zones) {
        if (zone.type == material_zone_type::unknown) {
            continue;
        }

        const terrain_material material = material_from_zone(zone.type);
        if (zone.has_radius && zone.radius > 0.00001f) {
            const uint32_t center_index = append_sampled_vertex(zone.center, material);
            uint32_t previous_edge = 0U;
            uint32_t first_edge = 0U;

            for (int i = 0; i < radius_segments; ++i) {
                const float angle = 2.0f * 3.14159265358979323846f *
                    static_cast<float>(i) / static_cast<float>(radius_segments);
                const glm::vec3 edge(zone.center.x + std::cos(angle) * zone.radius,
                                     zone.center.y,
                                     zone.center.z + std::sin(angle) * zone.radius);
                const uint32_t edge_index = append_sampled_vertex(edge, material);
                if (i == 0) {
                    first_edge = edge_index;
                } else {
                    mesh.indices.push_back(center_index);
                    mesh.indices.push_back(previous_edge);
                    mesh.indices.push_back(edge_index);
                }
                previous_edge = edge_index;
            }

            mesh.indices.push_back(center_index);
            mesh.indices.push_back(previous_edge);
            mesh.indices.push_back(first_edge);
            continue;
        }

        if (zone.has_bounds) {
            const glm::vec3 min_point(glm::min(zone.bounds_min, zone.bounds_max));
            const glm::vec3 max_point(glm::max(zone.bounds_min, zone.bounds_max));
            const std::uint32_t offset = static_cast<std::uint32_t>(mesh.vertices.size());

            for (int row = 0; row < bounds_resolution; ++row) {
                const float v = static_cast<float>(row) / static_cast<float>(bounds_resolution - 1);
                const float z = min_point.z + (max_point.z - min_point.z) * v;
                for (int column = 0; column < bounds_resolution; ++column) {
                    const float u = static_cast<float>(column) / static_cast<float>(bounds_resolution - 1);
                    const float x = min_point.x + (max_point.x - min_point.x) * u;
                    const float y = min_point.y + (max_point.y - min_point.y) * ((u + v) * 0.5f);
                    append_sampled_vertex(glm::vec3(x, y, z), material);
                }
            }

            for (int row = 0; row < bounds_resolution - 1; ++row) {
                for (int column = 0; column < bounds_resolution - 1; ++column) {
                    const uint32_t a = offset + static_cast<uint32_t>(row * bounds_resolution + column);
                    const uint32_t b = offset + static_cast<uint32_t>((row + 1) * bounds_resolution + column);
                    const uint32_t c = offset + static_cast<uint32_t>(row * bounds_resolution + column + 1);
                    const uint32_t d = offset + static_cast<uint32_t>((row + 1) * bounds_resolution + column + 1);
                    mesh.indices.push_back(a);
                    mesh.indices.push_back(b);
                    mesh.indices.push_back(c);
                    mesh.indices.push_back(c);
                    mesh.indices.push_back(b);
                    mesh.indices.push_back(d);
                }
            }
        }
    }

    mesh.section_count = 1;
    mesh.cross_section_count = 0;
    mesh.spatial_index = make_terrain_mesh_index(mesh);
    return mesh;
}

terrain_mesh build_outer_rough_apron(const terrain_mesh& source_mesh,
                                     const float margin,
                                     const int grid_resolution) {
    terrain_mesh apron;
    if (source_mesh.vertices.empty() || source_mesh.indices.size() < 3U) {
        return apron;
    }

    glm::vec3 min_point(std::numeric_limits<float>::max());
    glm::vec3 max_point(std::numeric_limits<float>::lowest());
    for (const terrain_vertex& vertex : source_mesh.vertices) {
        min_point.x = std::min(min_point.x, vertex.position.x);
        min_point.z = std::min(min_point.z, vertex.position.z);
        max_point.x = std::max(max_point.x, vertex.position.x);
        max_point.z = std::max(max_point.z, vertex.position.z);
    }

    const int resolution = std::max(2, grid_resolution);
    const float apron_margin = std::max(1.0f, margin);
    min_point.x -= apron_margin;
    min_point.z -= apron_margin;
    max_point.x += apron_margin;
    max_point.z += apron_margin;

    apron.section_count = resolution;
    apron.cross_section_count = resolution;
    apron.width = std::max(max_point.x - min_point.x, max_point.z - min_point.z);
    apron.vertices.reserve(static_cast<std::size_t>(resolution * resolution));
    apron.indices.reserve(static_cast<std::size_t>((resolution - 1) * (resolution - 1) * 6));

    constexpr float overlap_lowering = 0.12f;
    for (int row = 0; row < resolution; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(resolution - 1);
        const float z = min_point.z + (max_point.z - min_point.z) * v;
        for (int column = 0; column < resolution; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(resolution - 1);
            const float x = min_point.x + (max_point.x - min_point.x) * u;
            const glm::vec3 query(x, 0.0f, z);
            terrain_sample sample = sample_terrain_anchor(source_mesh, query, 0.0f);

            terrain_vertex vertex;
            vertex.position = glm::vec3(x, sample.point.y - overlap_lowering, z);
            vertex.normal = glm::vec3(0.0f);
            vertex.distance_from_center = source_mesh.width * 0.5f;
            vertex.material = terrain_material::rough;
            apron.vertices.push_back(vertex);
        }
    }

    for (int row = 0; row < resolution - 1; ++row) {
        for (int column = 0; column < resolution - 1; ++column) {
            const uint32_t a = static_cast<uint32_t>(row * resolution + column);
            const uint32_t b = static_cast<uint32_t>((row + 1) * resolution + column);
            const uint32_t c = static_cast<uint32_t>(row * resolution + column + 1);
            const uint32_t d = static_cast<uint32_t>((row + 1) * resolution + column + 1);
            apron.indices.push_back(a);
            apron.indices.push_back(b);
            apron.indices.push_back(c);
            apron.indices.push_back(c);
            apron.indices.push_back(b);
            apron.indices.push_back(d);
        }
    }

    for (std::size_t i = 0; i + 2U < apron.indices.size(); i += 3U) {
        const uint32_t ia = apron.indices[i];
        const uint32_t ib = apron.indices[i + 1U];
        const uint32_t ic = apron.indices[i + 2U];
        glm::vec3 normal = triangle_normal(apron.vertices[ia].position, apron.vertices[ib].position, apron.vertices[ic].position);
        if (normal.y < 0.0f) {
            normal = -normal;
        }
        add_vertex_normal(apron.vertices, ia, normal);
        add_vertex_normal(apron.vertices, ib, normal);
        add_vertex_normal(apron.vertices, ic, normal);
    }

    for (terrain_vertex& vertex : apron.vertices) {
        vertex.normal = safe_normalize(vertex.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        if (vertex.normal.y < 0.0f) {
            vertex.normal = -vertex.normal;
        }
    }

    apron.spatial_index = make_terrain_mesh_index(apron);
    return apron;
}

terrain_mesh build_terrain_mesh_index(terrain_mesh mesh) {
    mesh.spatial_index = make_terrain_mesh_index(mesh);
    return mesh;
}

terrain_sample sample_terrain_mesh(const terrain_mesh& mesh, const glm::vec3& position, const float fallback_y) {
    return sample_terrain_mesh(mesh, position, fallback_y, nullptr);
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
    const int preferred_section = previous_sample && previous_sample->triangle_index >= 0
        ? triangle_section_index(mesh, previous_sample->triangle_index)
        : -1;

    const terrain_mesh_index& index = mesh.spatial_index;

    // Fallback: no index, or an index that no longer describes this mesh.
    // Exactly the original full scan.
    if (!terrain_index_matches(mesh, index)) {
        terrain_scan_state state;
        state.best_edge.sample.point = query_point;
        for (int triangle = 0; triangle < triangle_count; ++triangle) {
            state = scan_triangle(mesh, position, query_point, fallback_y, preferred_section,
                                  triangle, true, true, state);
        }
        return finish_terrain_scan(state, query_point);
    }

    // Pass 1 — containing triangles.
    // A triangle can only contain the query in XZ if its padded bounding box
    // contains it, so every candidate the full scan would find lives in the
    // single cell the query falls into. Cell lists are stored in ascending
    // triangle order, so the winner is bit-identical to the full scan's.
    terrain_scan_state state;
    state.best_edge.sample.point = query_point;
    const int cell_x = index_cell_coord(position.x, index.min_x, index.cell_size_x, index.cells_x);
    const int cell_z = index_cell_coord(position.z, index.min_z, index.cell_size_z, index.cells_z);
    const std::size_t cell = static_cast<std::size_t>(cell_z) * static_cast<std::size_t>(index.cells_x)
        + static_cast<std::size_t>(cell_x);
    for (uint32_t slot = index.cell_starts[cell]; slot < index.cell_starts[cell + 1U]; ++slot) {
        state = scan_triangle(mesh, position, query_point, fallback_y, preferred_section,
                              static_cast<int>(index.cell_triangles[slot]), true, false, state);
    }
    if (state.has_inside) {
        return finish_terrain_scan(state, query_point);
    }

    // Pass 2 — no triangle contains the query, so fall back to the nearest
    // triangle edge. This is the only place the original scan looked at
    // faraway geometry.
    const bool has_real_sections = mesh.section_count > 0 && mesh.cross_section_count >= 2;
    if (preferred_section >= 0 && has_real_sections) {
        // is_better_edge_candidate ranks any candidate from the previous
        // sample's section above every other candidate, and with no containing
        // triangle every triangle in that section produces edge candidates.
        // So the full scan's winner is decided entirely inside that section's
        // contiguous triangle range.
        const int per_section = (mesh.cross_section_count - 1) * 2;
        const int first = per_section > 0 ? preferred_section * per_section : triangle_count;
        if (first < triangle_count) {
            const int last = preferred_section >= mesh.section_count - 1
                ? triangle_count
                : std::min(first + per_section, triangle_count);
            for (int triangle = first; triangle < last; ++triangle) {
                state = scan_triangle(mesh, position, query_point, fallback_y, preferred_section,
                                      triangle, false, true, state);
            }
            if (state.has_edge) {
                return finish_terrain_scan(state, query_point);
            }
        }
    }

    // Expanding-ring search over the grid. Stops once nothing unvisited can be
    // closer than the best edge distance found so far, so the gathered set is a
    // superset of every triangle that could win.
    std::vector<uint32_t> candidates;
    float best_distance = std::numeric_limits<float>::max();
    const int max_radius = std::max(index.cells_x, index.cells_z);
    for (int radius = 0; radius <= max_radius; ++radius) {
        const int x0 = std::max(0, cell_x - radius);
        const int x1 = std::min(index.cells_x - 1, cell_x + radius);
        const int z0 = std::max(0, cell_z - radius);
        const int z1 = std::min(index.cells_z - 1, cell_z + radius);
        for (int cz = z0; cz <= z1; ++cz) {
            const bool interior_row = cz > cell_z - radius && cz < cell_z + radius;
            for (int cx = x0; cx <= x1; ++cx) {
                if (radius > 0 && interior_row && cx > cell_x - radius && cx < cell_x + radius) {
                    continue;
                }
                const std::size_t ring_cell = static_cast<std::size_t>(cz) * static_cast<std::size_t>(index.cells_x)
                    + static_cast<std::size_t>(cx);
                for (uint32_t slot = index.cell_starts[ring_cell]; slot < index.cell_starts[ring_cell + 1U]; ++slot) {
                    const uint32_t triangle = index.cell_triangles[slot];
                    candidates.push_back(triangle);
                    best_distance = std::min(best_distance,
                                             triangle_edge_distance_squared(mesh, static_cast<int>(triangle),
                                                                            position, query_point));
                }
            }
        }

        if (x0 == 0 && z0 == 0 && x1 == index.cells_x - 1 && z1 == index.cells_z - 1) {
            break;
        }
        const float lower_bound = unvisited_lower_bound(index, x0, x1, z0, z1, position.x, position.z);
        if (lower_bound > best_distance + index_prune_slack) {
            break;
        }
    }

    // Replay the gathered triangles in ascending order, exactly the relative
    // order the full scan visited them in.
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    for (const uint32_t triangle : candidates) {
        state = scan_triangle(mesh, position, query_point, fallback_y, preferred_section,
                              static_cast<int>(triangle), false, true, state);
    }
    return finish_terrain_scan(state, query_point);
}

terrain_sample sample_terrain(const terrain_spline& terrain, const glm::vec3& position, const float fallback_y) {
    const terrain_mesh mesh = build_terrain_mesh(terrain);
    return sample_terrain_mesh(mesh, position, fallback_y);
}

terrain_sample sample_terrain_anchor(const terrain_mesh& mesh, const glm::vec3& position, const float fallback_y) {
    terrain_sample sample = sample_terrain_mesh(mesh, position, fallback_y);
    sample.point.x = position.x;
    sample.point.z = position.z;
    return sample;
}
