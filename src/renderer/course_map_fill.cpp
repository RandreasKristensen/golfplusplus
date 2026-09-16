#include "renderer/course_map_fill.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
void add_triangle_scan_intersection(const glm::vec2 a,
                                    const glm::vec2 b,
                                    const float y,
                                    std::array<float, 3>& intersections,
                                    int& intersection_count) {
    const float min_y = std::min(a.y, b.y);
    const float max_y = std::max(a.y, b.y);
    if (std::abs(a.y - b.y) < 0.00001f || y < min_y || y >= max_y || intersection_count >= 3) {
        return;
    }

    const float t = (y - a.y) / (b.y - a.y);
    intersections[static_cast<std::size_t>(intersection_count)] = a.x + (b.x - a.x) * t;
    ++intersection_count;
}
}

bool course_map_layouts_match(const course_map_layout& a, const course_map_layout& b) {
    return a.center == b.center &&
           a.half_size == b.half_size &&
           a.world_center == b.world_center &&
           a.scale == b.scale;
}

glm::vec2 map_point(const course_map_layout& layout, const glm::vec3& position) {
    const glm::vec2 delta(layout.world_center.x - position.x, position.z - layout.world_center.z);
    return layout.center + delta * layout.scale;
}

void append_map_fill_triangle(overlay_batch& batch,
                              const course_map_layout& layout,
                              const glm::vec2 a,
                              const glm::vec2 b,
                              const glm::vec2 c,
                              const glm::vec3 color) {
    const float inset = 0.025f;
    const glm::vec2 clip_min = layout.center - layout.half_size + glm::vec2(inset);
    const glm::vec2 clip_max = layout.center + layout.half_size - glm::vec2(inset);

    const float min_x = std::min({a.x, b.x, c.x});
    const float max_x = std::max({a.x, b.x, c.x});
    const float min_y = std::min({a.y, b.y, c.y});
    const float max_y = std::max({a.y, b.y, c.y});
    if (max_x < clip_min.x || min_x > clip_max.x || max_y < clip_min.y || min_y > clip_max.y) {
        return;
    }

    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 0.000001f) {
        return;
    }

    const float strip_height = std::max(0.0045f, std::min(0.011f, layout.scale * 0.72f));
    const float y_start = std::max(min_y, clip_min.y);
    const float y_end = std::min(max_y, clip_max.y);
    const int first_strip = static_cast<int>(std::floor((y_start - clip_min.y) / strip_height));
    const int last_strip = static_cast<int>(std::ceil((y_end - clip_min.y) / strip_height));

    for (int strip = first_strip; strip < last_strip; ++strip) {
        const float y = clip_min.y + (static_cast<float>(strip) + 0.5f) * strip_height;
        if (y < y_start || y > y_end) {
            continue;
        }

        std::array<float, 3> intersections{};
        int intersection_count = 0;
        add_triangle_scan_intersection(a, b, y, intersections, intersection_count);
        add_triangle_scan_intersection(b, c, y, intersections, intersection_count);
        add_triangle_scan_intersection(c, a, y, intersections, intersection_count);
        if (intersection_count < 2) {
            continue;
        }

        std::sort(intersections.begin(), intersections.begin() + intersection_count);
        const float x0 = std::max(intersections[0], clip_min.x);
        const float x1 = std::min(intersections[static_cast<std::size_t>(intersection_count - 1)], clip_max.x);
        if (x1 <= x0) {
            continue;
        }

        draw_overlay_quad(batch,
                          glm::vec2((x0 + x1) * 0.5f, y),
                          glm::vec2((x1 - x0) * 0.5f, strip_height * 0.56f),
                          color,
                          0.82f);
    }
}

void append_course_map_fill(overlay_batch& batch,
                            const course_map_layout& layout,
                            const render_static_mesh* mesh) {
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3) {
        return;
    }

    for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const std::uint32_t ia = mesh->indices[i];
        const std::uint32_t ib = mesh->indices[i + 1];
        const std::uint32_t ic = mesh->indices[i + 2];
        if (ia >= mesh->vertices.size() ||
            ib >= mesh->vertices.size() ||
            ic >= mesh->vertices.size()) {
            continue;
        }

        const render_terrain_vertex& va = mesh->vertices[ia];
        const render_terrain_vertex& vb = mesh->vertices[ib];
        const render_terrain_vertex& vc = mesh->vertices[ic];
        const glm::vec3 average_color = (va.color + vb.color + vc.color) / 3.0f;
        const glm::vec3 ink = average_color * 0.72f + glm::vec3(0.10f, 0.08f, 0.04f);
        append_map_fill_triangle(batch,
                                 layout,
                                 map_point(layout, va.position),
                                 map_point(layout, vb.position),
                                 map_point(layout, vc.position),
                                 ink);
    }
}

bool update_course_map_fill_cache(course_map_fill_cache& cache,
                                  const course_map_layout& layout,
                                  const render_static_mesh* mesh) {
    const std::uint64_t mesh_revision = mesh != nullptr ? mesh->revision : 0U;
    const std::size_t vertex_count = mesh != nullptr ? mesh->vertices.size() : 0U;
    const std::size_t index_count = mesh != nullptr ? mesh->indices.size() : 0U;

    if (cache.valid &&
        cache.mesh == mesh &&
        cache.mesh_revision == mesh_revision &&
        cache.mesh_vertex_count == vertex_count &&
        cache.mesh_index_count == index_count &&
        course_map_layouts_match(cache.layout, layout)) {
        return false;
    }

    clear_overlay_batch(cache.fill);
    append_course_map_fill(cache.fill, layout, mesh);
    cache.layout = layout;
    cache.mesh = mesh;
    cache.mesh_revision = mesh_revision;
    cache.mesh_vertex_count = vertex_count;
    cache.mesh_index_count = index_count;
    cache.valid = true;
    ++cache.revision;
    return true;
}
