#include "doctest.h"

#include "renderer/course_map_fill.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_mesh.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
render_static_mesh make_test_terrain_mesh(const float span, const std::uint64_t revision) {
    render_static_mesh mesh;
    const int steps = 8;
    for (int z = 0; z <= steps; ++z) {
        for (int x = 0; x <= steps; ++x) {
            render_terrain_vertex vertex;
            const float fx = static_cast<float>(x) / static_cast<float>(steps);
            const float fz = static_cast<float>(z) / static_cast<float>(steps);
            vertex.position = glm::vec3((fx - 0.5f) * span, 0.0f, (fz - 0.5f) * span);
            vertex.color = glm::vec3(0.18f + fx * 0.1f, 0.42f, 0.18f + fz * 0.1f);
            mesh.vertices.push_back(vertex);
        }
    }

    const std::uint32_t stride = static_cast<std::uint32_t>(steps + 1);
    for (int z = 0; z < steps; ++z) {
        for (int x = 0; x < steps; ++x) {
            const std::uint32_t base = static_cast<std::uint32_t>(z) * stride + static_cast<std::uint32_t>(x);
            mesh.indices.push_back(base);
            mesh.indices.push_back(base + stride);
            mesh.indices.push_back(base + 1U);
            mesh.indices.push_back(base + 1U);
            mesh.indices.push_back(base + stride);
            mesh.indices.push_back(base + stride + 1U);
        }
    }

    mesh.bounds = compute_render_mesh_bounds(mesh.vertices);
    mesh.revision = revision;
    return mesh;
}

course_map_layout make_test_layout(const float scale) {
    course_map_layout layout;
    layout.world_center = glm::vec3(3.0f, 0.0f, -2.0f);
    layout.scale = scale;
    return layout;
}

bool same_vertices(const std::vector<overlay_vertex>& a, const std::vector<overlay_vertex>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].position != b[i].position || a[i].color != b[i].color) {
            return false;
        }
    }
    return true;
}
}

TEST_CASE("map fill produces geometry for a terrain mesh") {
    const render_static_mesh mesh = make_test_terrain_mesh(60.0f, 1U);
    overlay_batch batch;
    append_course_map_fill(batch, make_test_layout(0.02f), &mesh);

    CHECK(!(batch.vertices.empty()));
    CHECK(batch.vertices.size() % overlay_vertices_per_quad == 0U);
}

TEST_CASE("cached map fill vertices are identical to the uncached build") {
    const render_static_mesh mesh = make_test_terrain_mesh(60.0f, 7U);
    const course_map_layout layout = make_test_layout(0.02f);

    overlay_batch direct;
    append_course_map_fill(direct, layout, &mesh);

    course_map_fill_cache cache;
    CHECK(update_course_map_fill_cache(cache, layout, &mesh));
    CHECK(same_vertices(cache.fill.vertices, direct.vertices));

    // A second frame with the same inputs reuses the cached vertices.
    const std::uint64_t revision = cache.revision;
    CHECK(!(update_course_map_fill_cache(cache, layout, &mesh)));
    CHECK(cache.revision == revision);
    CHECK(same_vertices(cache.fill.vertices, direct.vertices));
}

TEST_CASE("map fill cache rebuilds when the layout changes") {
    const render_static_mesh mesh = make_test_terrain_mesh(60.0f, 2U);
    course_map_fill_cache cache;
    update_course_map_fill_cache(cache, make_test_layout(0.02f), &mesh);
    const std::uint64_t revision = cache.revision;

    const course_map_layout moved = make_test_layout(0.03f);
    CHECK(update_course_map_fill_cache(cache, moved, &mesh));
    CHECK(cache.revision == revision + 1U);

    overlay_batch direct;
    append_course_map_fill(direct, moved, &mesh);
    CHECK(same_vertices(cache.fill.vertices, direct.vertices));
}

TEST_CASE("map fill cache rebuilds when the terrain mesh changes") {
    const render_static_mesh first = make_test_terrain_mesh(60.0f, 3U);
    const render_static_mesh second = make_test_terrain_mesh(90.0f, 4U);
    const course_map_layout layout = make_test_layout(0.02f);

    course_map_fill_cache cache;
    update_course_map_fill_cache(cache, layout, &first);
    const std::size_t first_size = cache.fill.vertices.size();

    CHECK(update_course_map_fill_cache(cache, layout, &second));
    overlay_batch direct;
    append_course_map_fill(direct, layout, &second);
    CHECK(same_vertices(cache.fill.vertices, direct.vertices));
    CHECK(cache.fill.vertices.size() != first_size);

    // Losing the mesh empties the fill instead of leaving stale geometry.
    CHECK(update_course_map_fill_cache(cache, layout, nullptr));
    CHECK(cache.fill.vertices.empty());
}

TEST_CASE("layout comparison only matches exactly equal layouts") {
    const course_map_layout base = make_test_layout(0.02f);
    course_map_layout same = base;
    CHECK(course_map_layouts_match(base, same));

    same.world_center.x += 0.0001f;
    CHECK(!(course_map_layouts_match(base, same)));

    course_map_layout shifted = base;
    shifted.center.y += 0.001f;
    CHECK(!(course_map_layouts_match(base, shifted)));
}
