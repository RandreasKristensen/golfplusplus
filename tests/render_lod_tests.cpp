#include "doctest.h"

#include "renderer/render_mesh.h"
#include "renderer/render_mesh_chunks.h"
#include "renderer/render_mesh_lod.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

namespace {

constexpr int columns = 64;
constexpr float cell = 1.0f;

terrain_lod_style test_style() {
    terrain_lod_style style;
    style.levels = 3;
    style.finest_error_meters = 0.05f;
    style.error_ratio = 3.0f;
    style.attribute_error = 0.05f;
    style.mirror_max_error_pixels = 0.5f;
    return style;
}

float hill(const float x, const float z) {
    return 3.0f * std::sin(x * 0.11f) * std::cos(z * 0.13f) + 0.02f * std::sin(x * 1.7f) * std::sin(z * 1.3f);
}

// A bumpy hill on a grid, `blades` 1 for x under half the grid, in chunks of 16 m.
render_static_mesh make_hill() {
    render_static_mesh mesh;
    for (int row = 0; row <= columns; ++row) {
        for (int column = 0; column <= columns; ++column) {
            render_terrain_vertex vertex;
            const float x = static_cast<float>(column) * cell;
            const float z = static_cast<float>(row) * cell;
            vertex.position = glm::vec3(x, hill(x, z), z);
            vertex.set_blades(column < columns / 2 ? 1.0f : 0.0f);
            mesh.vertices.push_back(vertex);
        }
    }
    const auto at = [](const int row, const int column) { return static_cast<std::uint32_t>(row * (columns + 1) + column); };
    for (int tile_row = 0; tile_row < columns; tile_row += 16) {
        for (int tile_column = 0; tile_column < columns; tile_column += 16) {
            for (int row = tile_row; row < tile_row + 16; ++row) {
                for (int column = tile_column; column < tile_column + 16; ++column) {
                    const std::uint32_t a = at(row, column);
                    const std::uint32_t b = at(row, column + 1);
                    const std::uint32_t c = at(row + 1, column);
                    const std::uint32_t d = at(row + 1, column + 1);
                    for (const std::uint32_t index : {a, c, b, b, c, d}) {
                        mesh.indices.push_back(index);
                    }
                }
            }
        }
    }
    mesh.bounds = compute_render_mesh_bounds(mesh.vertices);
    render_chunk_settings settings;
    settings.target_extent = 16.0f;
    mesh.chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices, settings);
    return mesh;
}

std::vector<std::uint32_t> list_of(const render_static_mesh& mesh, const render_mesh_chunk& chunk, const std::size_t level) {
    if (level == 0U) {
        return {mesh.indices.begin() + chunk.first_index, mesh.indices.begin() + chunk.first_index + chunk.index_count};
    }
    const render_mesh_chunk::span& span = chunk.coarse[level - 1U];
    if (span.count == 0U) {
        return list_of(mesh, chunk, 0U);
    }
    return {mesh.lod_indices.begin() + span.first, mesh.lod_indices.begin() + span.first + span.count};
}

// The surface's height and blades at (x, z) in one triangle list; false off it.
bool sample(const render_static_mesh& mesh, const std::vector<std::uint32_t>& list, const float x, const float z, float& height,
            float& blades) {
    for (std::size_t i = 0; i + 2U < list.size(); i += 3U) {
        const glm::vec3& a = mesh.vertices[list[i]].position;
        const glm::vec3& b = mesh.vertices[list[i + 1U]].position;
        const glm::vec3& c = mesh.vertices[list[i + 2U]].position;
        const float area = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
        const float wa = ((b.x - x) * (c.z - z) - (b.z - z) * (c.x - x)) / area;
        const float wb = ((x - a.x) * (c.z - a.z) - (z - a.z) * (c.x - a.x)) / area;
        const float wc = 1.0f - wa - wb;
        if (wa >= -1.0e-5f && wb >= -1.0e-5f && wc >= -1.0e-5f) {
            height = wa * a.y + wb * b.y + wc * c.y;
            blades = wa * mesh.vertices[list[i]].blades() + wb * mesh.vertices[list[i + 1U]].blades() +
                wc * mesh.vertices[list[i + 2U]].blades();
            return true;
        }
    }
    return false;
}

// The edges only one triangle of `list` uses.
std::set<std::pair<std::uint32_t, std::uint32_t>> border_of(const std::vector<std::uint32_t>& list) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> uses;
    for (std::size_t i = 0; i + 2U < list.size(); i += 3U) {
        for (std::size_t k = 0; k < 3U; ++k) {
            const std::uint32_t a = list[i + k];
            const std::uint32_t b = list[i + (k + 1U) % 3U];
            ++uses[{std::min(a, b), std::max(a, b)}];
        }
    }
    std::set<std::pair<std::uint32_t, std::uint32_t>> border;
    for (const auto& [edge, count] : uses) {
        if (count == 1) {
            border.insert(edge);
        }
    }
    return border;
}

}

TEST_CASE("coarser levels of a hill have fewer triangles and stay within their error of it") {
    render_static_mesh mesh = make_hill();
    build_render_mesh_lod(mesh, test_style());
    REQUIRE(mesh.lod_error_meters.size() == 3U);
    CHECK(std::abs(mesh.lod_error_meters[1] - 0.15f) < 1.0e-5f);
    REQUIRE(!mesh.lod_indices.empty());

    std::size_t full = 0;
    std::size_t counts[3] = {};
    for (const render_mesh_chunk& chunk : mesh.chunks) {
        full += chunk.index_count;
        for (std::size_t level = 0; level < 3U; ++level) {
            counts[level] += list_of(mesh, chunk, level + 1U).size();
        }
    }
    CHECK(counts[0] < full);
    CHECK(counts[2] < counts[0]);

    for (const render_mesh_chunk& chunk : mesh.chunks) {
        const std::vector<std::uint32_t> whole = list_of(mesh, chunk, 0U);
        for (std::size_t level = 1; level <= 3U; ++level) {
            const std::vector<std::uint32_t> coarse = list_of(mesh, chunk, level);
            for (int i = 0; i < 40; ++i) {
                for (int j = 0; j < 40; ++j) {
                    const float x = chunk.bounds.min.x + (chunk.bounds.max.x - chunk.bounds.min.x) * (static_cast<float>(i) + 0.37f) / 40.0f;
                    const float z = chunk.bounds.min.z + (chunk.bounds.max.z - chunk.bounds.min.z) * (static_cast<float>(j) + 0.61f) / 40.0f;
                    float full_height = 0.0f;
                    float coarse_height = 0.0f;
                    float blades = 0.0f;
                    if (sample(mesh, whole, x, z, full_height, blades) && sample(mesh, coarse, x, z, coarse_height, blades)) {
                                                CHECK(std::abs(full_height - coarse_height) <= mesh.lod_error_meters[level - 1U]);
                    }
                }
            }
        }
    }
}

TEST_CASE("every level of a chunk keeps its border, so chunks at different levels meet without a crack") {
    render_static_mesh mesh = make_hill();
    build_render_mesh_lod(mesh, test_style());
    for (const render_mesh_chunk& chunk : mesh.chunks) {
        const auto border = border_of(list_of(mesh, chunk, 0U));
        for (std::size_t level = 1; level <= 3U; ++level) {
            CHECK(border_of(list_of(mesh, chunk, level)) == border);
        }
    }
}

TEST_CASE("a material edge stays where the full mesh has it") {
    render_static_mesh mesh = make_hill();
    build_render_mesh_lod(mesh, test_style());
    for (const render_mesh_chunk& chunk : mesh.chunks) {
        const std::vector<std::uint32_t> whole = list_of(mesh, chunk, 0U);
        const std::vector<std::uint32_t> coarse = list_of(mesh, chunk, 3U);
        for (float x = chunk.bounds.min.x + 0.2f; x < chunk.bounds.max.x; x += 0.7f) {
            for (float z = chunk.bounds.min.z + 0.3f; z < chunk.bounds.max.z; z += 0.9f) {
                float height = 0.0f;
                float full_blades = 0.0f;
                float coarse_blades = 0.0f;
                if (sample(mesh, whole, x, z, height, full_blades) && sample(mesh, coarse, x, z, height, coarse_blades)) {
                    CHECK(std::abs(coarse_blades - full_blades) < 0.001f);
                }
            }
        }
    }
}

TEST_CASE("a pass draws a chunk as a coarser level only where its error spans few enough pixels") {
    render_static_mesh mesh = make_hill();
    build_render_mesh_lod(mesh, test_style());
    const render_mesh_chunk& chunk = mesh.chunks.front();
    render_lod_view view;
    view.focal_pixels = 100.0f;
    view.max_error_pixels = 0.5f;
    const glm::vec3 center = 0.5f * (chunk.bounds.min + chunk.bounds.max);

    view.eye = center + glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(choose_chunk_lod_level(chunk, mesh.lod_error_meters, view) == 0U);
    view.eye = center + glm::vec3(0.0f, 0.0f, 40.0f);
    const std::size_t middle = choose_chunk_lod_level(chunk, mesh.lod_error_meters, view);
    view.eye = center + glm::vec3(0.0f, 0.0f, 4000.0f);
    const std::size_t far = choose_chunk_lod_level(chunk, mesh.lod_error_meters, view);
    CHECK(far == 3U);
    CHECK(far >= middle);
    view.max_error_pixels = 0.0f;
    CHECK(choose_chunk_lod_level(chunk, mesh.lod_error_meters, view) == 0U);
}

TEST_CASE("coarse ranges join only with their own level") {
    render_static_mesh mesh = make_hill();
    build_render_mesh_lod(mesh, test_style());
    std::vector<render_index_range> ranges;
    append_render_index_range(ranges, 0U, 30U, 0);
    append_render_index_range(ranges, 30U, 30U, 1);
    append_render_index_range(ranges, 60U, 30U, 1);
    REQUIRE(ranges.size() == 2U);
    CHECK(ranges[1].index_count == 60U);
    limit_render_index_ranges(ranges, 1U);
    CHECK(ranges.size() == 2U);
}
