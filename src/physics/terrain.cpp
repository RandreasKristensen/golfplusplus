#include "physics/terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace {
float clamp01(const float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float distance_xz_squared(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
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

int terrain_section_count(const terrain_spline& terrain) {
    const float length = control_polygon_xz_length(terrain.control_points);
    const int length_sections = static_cast<int>(std::ceil(length)) + 1;
    return std::max(2, std::max(terrain.sample_count, length_sections));
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
    sample.material = inside_surface ? terrain_material::fairway : terrain_material::rough;
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
    terrain_mesh mesh;
    if (terrain.control_points.size() < 2 || terrain.width <= 0.00001f) {
        return mesh;
    }

    mesh.section_count = terrain_section_count(terrain);
    mesh.cross_section_count = 9;
    mesh.width = terrain.width;
    mesh.vertices.reserve(static_cast<std::size_t>(mesh.section_count * mesh.cross_section_count));
    mesh.indices.reserve(static_cast<std::size_t>((mesh.section_count - 1) * (mesh.cross_section_count - 1) * 6));

    for (int section = 0; section < mesh.section_count; ++section) {
        const float t = static_cast<float>(section) / static_cast<float>(mesh.section_count - 1);
        const glm::vec3 center = sample_terrain_spline_point(terrain, t);
        const glm::vec3 lateral = terrain_lateral_at(terrain, t);

        for (int column = 0; column < mesh.cross_section_count; ++column) {
            const float offset = cross_section_offset(column, mesh.cross_section_count, terrain.width);
            terrain_vertex vertex;
            vertex.position = center + lateral * offset;
            vertex.position.y += cross_section_height_offset(offset, terrain.width);
            vertex.normal = glm::vec3(0.0f);
            vertex.distance_from_center = offset;
            vertex.material = terrain_material::fairway;
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

    return mesh;
}

terrain_sample sample_terrain_mesh(const terrain_mesh& mesh, const glm::vec3& position, const float fallback_y) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3U) {
        terrain_sample sample;
        sample.point = glm::vec3(position.x, fallback_y, position.z);
        return sample;
    }

    float best_distance = std::numeric_limits<float>::max();
    terrain_sample best_sample;
    best_sample.point = glm::vec3(position.x, fallback_y, position.z);

    for (std::size_t i = 0; i + 2U < mesh.indices.size(); i += 3U) {
        const terrain_vertex& a = mesh.vertices[mesh.indices[i]];
        const terrain_vertex& b = mesh.vertices[mesh.indices[i + 1U]];
        const terrain_vertex& c = mesh.vertices[mesh.indices[i + 2U]];

        glm::vec3 barycentric(0.0f);
        if (barycentric_xz(position, a.position, b.position, c.position, barycentric)) {
            terrain_sample sample = sample_from_barycentric(mesh, static_cast<int>(i / 3U), barycentric, true);
            sample.point.x = position.x;
            sample.point.z = position.z;
            return sample;
        }

        for (int edge = 0; edge < 3; ++edge) {
            const glm::vec3 edge_barycentric = closest_barycentric_on_edge(position, a.position, b.position, c.position, edge);
            terrain_sample edge_sample = sample_from_barycentric(mesh, static_cast<int>(i / 3U), edge_barycentric, false);
            const float distance = distance_xz_squared(position, edge_sample.point);
            if (distance < best_distance) {
                best_distance = distance;
                edge_sample.distance_from_center += std::sqrt(distance);
                best_sample = edge_sample;
            }
        }
    }

    best_sample.has_spline = true;
    best_sample.inside_surface = false;
    best_sample.material = terrain_material::rough;
    return best_sample;
}

terrain_sample sample_terrain(const terrain_spline& terrain, const glm::vec3& position, const float fallback_y) {
    const terrain_mesh mesh = build_terrain_mesh(terrain);
    return sample_terrain_mesh(mesh, position, fallback_y);
}
