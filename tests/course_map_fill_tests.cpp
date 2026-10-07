#include "doctest.h"

#include "renderer/course_map_fill.h"
#include "renderer/course_map_overlay.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_mesh.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

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
    layout.scale = glm::vec2(scale);
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

TEST_CASE("map fill prints triangles smaller than a strip") {
    overlay_batch batch;
    const course_map_layout layout = make_test_layout(0.02f);
    const glm::vec2 corner = layout.center;
    append_map_fill_triangle(batch, layout, corner, corner + glm::vec2(0.0008f, 0.0f), corner + glm::vec2(0.0f, 0.0008f),
                             glm::vec3(1.0f));
    CHECK(overlay_batch_quad_count(batch) == 1U);
}

TEST_CASE("the map prints water in its own ink, never the bunkers'") {
    const glm::vec3 water = course_map_material_ink(terrain_material::water);
    const glm::vec3 bunker = course_map_material_ink(terrain_material::bunker);
    CHECK(glm::length(water - bunker) > 0.3f);
    CHECK(water.b > water.r);
    CHECK(water.b > water.g);
}

TEST_CASE("the course map fits the course's holes to its paper, square on screen, at every aspect") {
    render_data data;
    data.course_map_low = glm::vec3(-100.0f, 0.0f, 50.0f);
    data.course_map_high = glm::vec3(300.0f, 0.0f, 950.0f);
    data.player_position = glm::vec3(0.0f, 0.0f, 100.0f);
    for (const overlay_grid grid : {overlay_grid{554, 416}, overlay_grid{640, 360}, overlay_grid{733, 314}}) {
        const course_map_layout layout = make_course_map_layout(data, grid);
        // One world unit is as many pixels across as up.
        CHECK(std::abs(layout.scale.x * static_cast<float>(grid.width) - layout.scale.y * static_cast<float>(grid.height)) <
              0.0001f);
        const glm::vec2 a = map_point(layout, data.course_map_low);
        const glm::vec2 b = map_point(layout, data.course_map_high);
        const glm::vec2 low = glm::min(a, b);
        const glm::vec2 high = glm::max(a, b);
        const glm::vec2 paper_low = layout.center - layout.half_size;
        const glm::vec2 paper_high = layout.center + layout.half_size;
        CHECK(low.x > paper_low.x);
        CHECK(low.y > paper_low.y);
        CHECK(high.x < paper_high.x);
        CHECK(high.y < paper_high.y);
        // The course fills the paper along its longer side, less a small margin.
        const glm::vec2 fill = (high - low) / (layout.half_size * 2.0f);
        CHECK(std::max(fill.x, fill.y) > 0.8f);
    }
}

TEST_CASE("every hole gets its number on the course map, inside the paper and clear of the others") {
    render_data data;
    data.course_map_low = glm::vec3(0.0f);
    data.course_map_high = glm::vec3(600.0f, 0.0f, 600.0f);
    for (int i = 0; i < 18; ++i) {
        data.map_holes.push_back(render_map_hole{std::to_string(i + 1), glm::vec3(static_cast<float>(i % 6) * 100.0f, 0.0f,
                                                                                    static_cast<float>(i / 6) * 250.0f)});
    }
    // Two tees side by side, as at a clubhouse.
    data.map_holes.push_back(render_map_hole{"19", data.map_holes.front().tee + glm::vec3(2.0f, 0.0f, 0.0f)});
    const text_assets& text = shipped_text_assets();
    for (const overlay_grid grid : {overlay_grid{554, 416}, overlay_grid{640, 360}, overlay_grid{733, 314}}) {
        const course_map_layout layout = make_course_map_layout(data, grid);
        const std::vector<ui_rect> boxes = course_map_number_boxes(layout, grid, data.map_holes);
        REQUIRE(boxes.size() == data.map_holes.size());
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            CHECK(rect_left(boxes[i]) >= layout.center.x - layout.half_size.x - 0.0001f);
            CHECK(rect_right(boxes[i]) <= layout.center.x + layout.half_size.x + 0.0001f);
            CHECK(rect_bottom(boxes[i]) >= layout.center.y - layout.half_size.y - 0.0001f);
            CHECK(rect_top(boxes[i]) <= layout.center.y + layout.half_size.y + 0.0001f);
            for (std::size_t j = 0; j < i; ++j) {
                const glm::vec2 gap = glm::abs(boxes[i].center - boxes[j].center) - (boxes[i].half_size + boxes[j].half_size);
                CHECK((gap.x >= 0.0f || gap.y >= 0.0f));
            }
        }
        overlay_batch batch;
        batch.grid = grid;
        draw_course_map_marks(batch, text, layout, data);
        CHECK(batch.truncated_text_count == 0U);
    }
}
