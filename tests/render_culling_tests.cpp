#include "doctest.h"

#include "game/course_loader.h"
#include "game/game_state.h"
#include "physics/terrain.h"
#include "renderer/frustum.h"
#include "renderer/render_mesh.h"
#include "renderer/render_mesh_chunks.h"
#include "renderer/render_tree.h"
#include "renderer/terrain_render_mesh.h"

#include "test_support.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

// A camera at the origin looking down -Z, matching the renderer's projection
// convention (GLM perspective, OpenGL clip space).
view_frustum make_test_frustum(const glm::vec3& eye = glm::vec3(0.0f),
                               const glm::vec3& target = glm::vec3(0.0f, 0.0f, -1.0f),
                               const float far_plane = 100.0f) {
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, far_plane);
    const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    return make_view_frustum(proj * view);
}

bool box_visible(const view_frustum& frustum, const glm::vec3& center, const float half_size) {
    return frustum_intersects_aabb(frustum,
                                   center - glm::vec3(half_size),
                                   center + glm::vec3(half_size));
}

render_terrain_vertex make_vertex(const float x, const float y, const float z) {
    render_terrain_vertex vertex;
    vertex.position = glm::vec3(x, y, z);
    return vertex;
}

// A grid of quads spanning [0, size_x] x [0, size_z] in XZ, emitted row by row.
struct test_grid_mesh {
    std::vector<render_terrain_vertex> vertices;
    std::vector<std::uint32_t> indices;
};

test_grid_mesh make_grid_mesh(const int columns, const int rows, const float cell) {
    test_grid_mesh mesh;
    for (int row = 0; row <= rows; ++row) {
        for (int column = 0; column <= columns; ++column) {
            mesh.vertices.push_back(make_vertex(static_cast<float>(column) * cell,
                                                0.0f,
                                                static_cast<float>(row) * cell));
        }
    }

    const std::uint32_t stride = static_cast<std::uint32_t>(columns + 1);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const std::uint32_t a = static_cast<std::uint32_t>(row) * stride + static_cast<std::uint32_t>(column);
            const std::uint32_t b = a + 1U;
            const std::uint32_t c = a + stride;
            const std::uint32_t d = c + 1U;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
        }
    }
    return mesh;
}

// Canonical triangle key so a reordered index buffer can be compared as a
// multiset of triangles.
std::vector<std::uint32_t> triangle_key(const std::vector<std::uint32_t>& indices, const std::size_t first) {
    std::vector<std::uint32_t> key{indices[first], indices[first + 1U], indices[first + 2U]};
    std::sort(key.begin(), key.end());
    return key;
}

std::map<std::vector<std::uint32_t>, int> triangle_multiset(const std::vector<std::uint32_t>& indices) {
    std::map<std::vector<std::uint32_t>, int> counts;
    for (std::size_t i = 0; i + 2U < indices.size(); i += 3U) {
        ++counts[triangle_key(indices, i)];
    }
    return counts;
}

} // namespace

TEST_CASE("frustum planes keep a box in front of the camera and cull one behind it") {
    const view_frustum frustum = make_test_frustum();
    CHECK(box_visible(frustum, glm::vec3(0.0f, 0.0f, -20.0f), 1.0f));
    CHECK(!box_visible(frustum, glm::vec3(0.0f, 0.0f, 20.0f), 1.0f));
}

TEST_CASE("frustum culls boxes far to the left and right") {
    const view_frustum frustum = make_test_frustum();
    CHECK(!box_visible(frustum, glm::vec3(-60.0f, 0.0f, -20.0f), 1.0f));
    CHECK(!box_visible(frustum, glm::vec3(60.0f, 0.0f, -20.0f), 1.0f));
    CHECK(!box_visible(frustum, glm::vec3(0.0f, 40.0f, -20.0f), 1.0f));
    CHECK(!box_visible(frustum, glm::vec3(0.0f, -40.0f, -20.0f), 1.0f));
}

TEST_CASE("frustum keeps a box straddling the left plane") {
    const view_frustum frustum = make_test_frustum();
    // Half width of the frustum at z = -20 for a 60 degree vertical fov, 16:9.
    const float half_height = 20.0f * std::tan(glm::radians(30.0f));
    const float half_width = half_height * (16.0f / 9.0f);
    CHECK(box_visible(frustum, glm::vec3(-half_width, 0.0f, -20.0f), 2.0f));
    CHECK(box_visible(frustum, glm::vec3(half_width, 0.0f, -20.0f), 2.0f));
    CHECK(!box_visible(frustum, glm::vec3(-half_width - 12.0f, 0.0f, -20.0f), 2.0f));
}

TEST_CASE("frustum keeps a box containing the camera and culls beyond the far plane") {
    const view_frustum frustum = make_test_frustum(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 100.0f);
    CHECK(box_visible(frustum, glm::vec3(0.0f), 5.0f));
    CHECK(!box_visible(frustum, glm::vec3(0.0f, 0.0f, -160.0f), 10.0f));
    CHECK(box_visible(frustum, glm::vec3(0.0f, 0.0f, -95.0f), 2.0f));
}

TEST_CASE("frustum follows a rotated camera") {
    const view_frustum frustum = make_test_frustum(glm::vec3(10.0f, 2.0f, 10.0f), glm::vec3(60.0f, 2.0f, 10.0f));
    CHECK(box_visible(frustum, glm::vec3(40.0f, 2.0f, 10.0f), 2.0f));
    CHECK(!box_visible(frustum, glm::vec3(-40.0f, 2.0f, 10.0f), 2.0f));
}

TEST_CASE("chunk builder covers every triangle exactly once with contiguous ranges") {
    const test_grid_mesh mesh = make_grid_mesh(16, 16, 4.0f);
    render_chunk_settings settings;
    settings.target_extent = 16.0f;
    const std::vector<render_mesh_chunk> chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices, settings);

    CHECK(chunks.size() > 1U);
    std::uint32_t expected_first = 0U;
    std::uint64_t total = 0U;
    for (const render_mesh_chunk& chunk : chunks) {
        CHECK(chunk.first_index == expected_first);
        CHECK(chunk.index_count > 0U);
        CHECK(chunk.index_count % 3U == 0U);
        expected_first += chunk.index_count;
        total += chunk.index_count;
    }
    CHECK(total == mesh.indices.size());
    CHECK(expected_first == mesh.indices.size());
}

TEST_CASE("chunk bounds contain every vertex of the chunk triangles") {
    const test_grid_mesh mesh = make_grid_mesh(16, 16, 4.0f);
    render_chunk_settings settings;
    settings.target_extent = 16.0f;
    const std::vector<render_mesh_chunk> chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices, settings);

    for (const render_mesh_chunk& chunk : chunks) {
        CHECK(chunk.bounds.valid);
        for (std::uint32_t i = 0; i < chunk.index_count; ++i) {
            const std::uint32_t index = mesh.indices[chunk.first_index + i];
            CHECK(index < mesh.vertices.size());
            const glm::vec3 position = mesh.vertices[index].position;
            CHECK(position.x >= chunk.bounds.min.x);
            CHECK(position.y >= chunk.bounds.min.y);
            CHECK(position.z >= chunk.bounds.min.z);
            CHECK(position.x <= chunk.bounds.max.x);
            CHECK(position.y <= chunk.bounds.max.y);
            CHECK(position.z <= chunk.bounds.max.z);
        }
    }
}

TEST_CASE("chunking preserves the triangle set and the triangle order") {
    const test_grid_mesh mesh = make_grid_mesh(12, 9, 3.0f);
    render_chunk_settings settings;
    settings.target_extent = 9.0f;
    const std::vector<render_mesh_chunk> chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices, settings);

    std::vector<std::uint32_t> replayed;
    for (const render_mesh_chunk& chunk : chunks) {
        for (std::uint32_t i = 0; i < chunk.index_count; ++i) {
            replayed.push_back(mesh.indices[chunk.first_index + i]);
        }
    }
    CHECK(replayed == mesh.indices);
    CHECK(triangle_multiset(replayed) == triangle_multiset(mesh.indices));
}

TEST_CASE("chunk builder handles empty meshes and oversized chunk targets") {
    CHECK(build_render_mesh_chunks({}, {}).empty());

    const test_grid_mesh mesh = make_grid_mesh(4, 4, 2.0f);
    CHECK(build_render_mesh_chunks(mesh.vertices, {}).empty());
    CHECK(build_render_mesh_chunks({}, mesh.indices).empty());

    render_chunk_settings huge;
    huge.target_extent = 10000.0f;
    const std::vector<render_mesh_chunk> single = build_render_mesh_chunks(mesh.vertices, mesh.indices, huge);
    CHECK(single.size() == 1U);
    CHECK(single.front().first_index == 0U);
    CHECK(single.front().index_count == mesh.indices.size());
}

TEST_CASE("chunk builder never exceeds the chunk budget") {
    const test_grid_mesh mesh = make_grid_mesh(24, 24, 4.0f);
    render_chunk_settings settings;
    settings.target_extent = 1.0f;
    settings.max_chunks = 12U;
    const std::vector<render_mesh_chunk> chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices, settings);
    CHECK(!chunks.empty());
    CHECK(chunks.size() <= 12U);

    std::uint64_t total = 0U;
    for (const render_mesh_chunk& chunk : chunks) {
        total += chunk.index_count;
    }
    CHECK(total == mesh.indices.size());
}

TEST_CASE("chunk builder drops trailing indices that are not a whole triangle") {
    test_grid_mesh mesh = make_grid_mesh(4, 4, 2.0f);
    const std::size_t triangle_indices = mesh.indices.size();
    mesh.indices.push_back(0U);
    mesh.indices.push_back(1U);

    const std::vector<render_mesh_chunk> chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices);
    std::uint64_t total = 0U;
    for (const render_mesh_chunk& chunk : chunks) {
        total += chunk.index_count;
    }
    CHECK(total == triangle_indices);
}

TEST_CASE("adjacent visible chunks merge into one draw range") {
    const test_grid_mesh mesh = make_grid_mesh(16, 16, 4.0f);
    render_chunk_settings settings;
    settings.target_extent = 16.0f;
    const std::vector<render_mesh_chunk> chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices, settings);
    CHECK(chunks.size() >= 4U);
    if (chunks.size() < 4U) {
        return;
    }

    std::vector<render_index_range> ranges;
    const std::vector<bool> all_visible(chunks.size(), true);
    const render_chunk_cull_stats all = collect_chunk_index_ranges(chunks,
                                                                   all_visible,
                                                                   mesh.indices.size(),
                                                                   8U,
                                                                   ranges);
    CHECK(ranges.size() == 1U);
    CHECK(all.draw_ranges == 1U);
    CHECK(all.chunks_visible == chunks.size());
    CHECK(all.chunks_culled == 0U);
    CHECK(all.indices_drawn == mesh.indices.size());

    std::vector<bool> two_runs(chunks.size(), false);
    two_runs[0] = true;
    two_runs[1] = true;
    two_runs[chunks.size() - 1U] = true;
    const render_chunk_cull_stats split = collect_chunk_index_ranges(chunks,
                                                                     two_runs,
                                                                     mesh.indices.size(),
                                                                     8U,
                                                                     ranges);
    CHECK(ranges.size() == 2U);
    CHECK(split.chunks_visible == 3U);
    CHECK(split.chunks_culled == chunks.size() - 3U);
    CHECK(ranges[0].first_index == chunks[0].first_index);
    CHECK(ranges[0].index_count == chunks[0].index_count + chunks[1].index_count);
    CHECK(ranges[1].first_index == chunks.back().first_index);

    std::vector<bool> none(chunks.size(), false);
    const render_chunk_cull_stats culled = collect_chunk_index_ranges(chunks, none, mesh.indices.size(), 8U, ranges);
    CHECK(ranges.empty());
    CHECK(culled.draw_ranges == 0U);
    CHECK(culled.indices_drawn == 0U);
    CHECK(culled.chunks_culled == chunks.size());
}

TEST_CASE("range limiting merges the smallest gap and never drops indices") {
    std::vector<render_index_range> ranges{
        render_index_range{0U, 30U},
        render_index_range{60U, 30U},
        render_index_range{300U, 30U}
    };
    limit_render_index_ranges(ranges, 2U);
    CHECK(ranges.size() == 2U);
    CHECK(ranges[0].first_index == 0U);
    CHECK(ranges[0].index_count == 90U);
    CHECK(ranges[1].first_index == 300U);
    CHECK(ranges[1].index_count == 30U);

    limit_render_index_ranges(ranges, 1U);
    CHECK(ranges.size() == 1U);
    CHECK(ranges[0].first_index == 0U);
    CHECK(ranges[0].index_count == 330U);
}

TEST_CASE("unchunked meshes fall back to a single full draw range") {
    std::vector<render_index_range> ranges;
    const render_chunk_cull_stats stats = collect_chunk_index_ranges({}, {}, 120U, 4U, ranges);
    CHECK(ranges.size() == 1U);
    CHECK(ranges[0].first_index == 0U);
    CHECK(ranges[0].index_count == 120U);
    CHECK(stats.chunks_total == 0U);
    CHECK(stats.indices_drawn == 120U);

    const render_chunk_cull_stats empty = collect_chunk_index_ranges({}, {}, 0U, 4U, ranges);
    CHECK(ranges.empty());
    CHECK(empty.indices_drawn == 0U);
}

TEST_CASE("chunk ranges are clamped to the uploaded index count") {
    std::vector<render_mesh_chunk> chunks(2);
    chunks[0].first_index = 0U;
    chunks[0].index_count = 60U;
    chunks[1].first_index = 60U;
    chunks[1].index_count = 60U;

    std::vector<render_index_range> ranges;
    const render_chunk_cull_stats stats = collect_chunk_index_ranges(chunks, {}, 90U, 4U, ranges);
    CHECK(ranges.size() == 1U);
    CHECK(ranges[0].index_count == 90U);
    CHECK(stats.indices_drawn == 90U);
}

TEST_CASE("frustum culling of chunks keeps the chunks the camera looks at") {
    const test_grid_mesh mesh = make_grid_mesh(16, 16, 4.0f);
    render_chunk_settings settings;
    settings.target_extent = 16.0f;
    const std::vector<render_mesh_chunk> chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices, settings);

    // Camera above the near corner of the grid looking away from it.
    const view_frustum away = make_test_frustum(glm::vec3(-20.0f, 5.0f, -20.0f), glm::vec3(-200.0f, 5.0f, -200.0f));
    std::vector<render_index_range> ranges;
    const render_chunk_cull_stats hidden = collect_visible_index_ranges(chunks, away, mesh.indices.size(), 8U, ranges);
    CHECK(hidden.chunks_visible == 0U);
    CHECK(hidden.indices_drawn == 0U);

    // Camera above the grid centre looking down at it sees everything.
    const view_frustum overhead = make_test_frustum(glm::vec3(32.0f, 90.0f, 32.0f), glm::vec3(32.0f, 0.0f, 32.0f), 400.0f);
    const render_chunk_cull_stats visible = collect_visible_index_ranges(chunks, overhead, mesh.indices.size(), 8U, ranges);
    CHECK(visible.chunks_visible == chunks.size());
    CHECK(visible.draw_ranges == 1U);

    // Camera at one edge looking across sees some but not all chunks.
    const view_frustum across = make_test_frustum(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(2.0f, 2.0f, 64.0f), 400.0f);
    const render_chunk_cull_stats partial = collect_visible_index_ranges(chunks, across, mesh.indices.size(), 8U, ranges);
    CHECK(partial.chunks_visible > 0U);
    CHECK(partial.chunks_visible < chunks.size());
    CHECK(partial.indices_drawn < mesh.indices.size());
}

TEST_CASE("tree instance bounds cover every trunk and leaf") {
    const std::vector<tree_body> trees{
        tree_body{glm::vec3(10.0f, 0.0f, -5.0f), tree_shape{0.4f, 3.0f, 2.0f, 4.0f}},
        tree_body{glm::vec3(-20.0f, 1.0f, 30.0f), tree_shape{0.35f, 2.4f, 1.6f, 3.2f}},
    };

    const render_tree_instance_batch batch = build_tree_instances(trees);
    const render_mesh_bounds bounds = compute_tree_instance_bounds(batch);
    CHECK(bounds.valid);
    CHECK(bounds.min.x <= 10.0f - 2.0f);
    CHECK(bounds.max.x >= 10.0f + 2.0f);
    CHECK(bounds.min.y <= 0.0f);
    CHECK(bounds.max.y >= 7.0f);
    CHECK(bounds.min.z <= -5.0f - 2.0f);
    CHECK(bounds.max.z >= 30.0f);

    CHECK(!compute_tree_instance_bounds(build_tree_instances({})).valid);
}

TEST_CASE("a hub's terrain chunks into bounded pieces that cull from ground level") {
    const game_state state = started_game(fixture_hub_course());
    const render_static_mesh mesh = make_terrain_render_mesh({&state.area.terrain, &state.area.apron}, 1U);
    REQUIRE(mesh.indices.size() > 1000U);

    const render_chunk_settings settings;
    CHECK(mesh.chunks.size() > 10U);
    CHECK(mesh.chunks.size() <= settings.max_chunks);

    std::uint64_t total = 0U;
    std::uint32_t expected_first = 0U;
    for (const render_mesh_chunk& chunk : mesh.chunks) {
        CHECK(chunk.first_index == expected_first);
        CHECK(chunk.bounds.valid);
        expected_first += chunk.index_count;
        total += chunk.index_count;
    }
    CHECK(total == mesh.indices.size());

    // Standing at hole 1 looking down it, most of the hub is off screen.
    const glm::vec3 eye = state.player.position + glm::vec3(0.0f, 1.7f, 0.0f);
    const view_frustum frustum = make_test_frustum(eye, eye + glm::vec3(0.0f, 0.0f, 40.0f), 400.0f);
    std::vector<render_index_range> ranges;
    const render_chunk_cull_stats stats = collect_visible_index_ranges(mesh.chunks, frustum, mesh.indices.size(), 4U, ranges);
    CHECK(stats.chunks_visible < mesh.chunks.size());
    CHECK(stats.draw_ranges <= 4U);
    CHECK(stats.indices_drawn * 2U < stats.indices_total);
}
