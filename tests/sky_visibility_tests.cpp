#include "doctest.h"

#include "physics/ground_mesh.h"
#include "physics/terrain.h"
#include "renderer/sky_visibility.h"
#include "renderer/terrain_render_mesh.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace {
// `size` x `size` points every `cell` metres from the origin, heights from `height`.
height_grid make_grid(const int size, const float cell, const std::function<float(float, float)>& height) {
    height_grid grid;
    grid.cell_size = cell;
    grid.columns = size;
    grid.rows = size;
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            grid.heights.push_back(height(static_cast<float>(column) * cell, static_cast<float>(row) * cell));
        }
    }
    return grid;
}

float grid_at(const height_grid& grid, const int column, const int row) {
    return grid.heights[static_cast<std::size_t>(row * grid.columns + column)];
}

// A bowl `depth` deep and `radius` across at (cx, cz), level ground around it.
float bowl(const float x, const float z, const float cx, const float cz, const float radius, const float depth) {
    const float t = std::min(1.0f, std::hypot(x - cx, z - cz) / radius);
    return -depth * (1.0f - t * t);
}

// Ground as a mesh: `size` x `size` vertices every `cell` metres, heights from `height`.
terrain_mesh make_ground(const int size, const float cell, const std::function<float(float, float)>& height) {
    terrain_mesh mesh;
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            const float x = static_cast<float>(column) * cell;
            const float z = static_cast<float>(row) * cell;
            terrain_vertex vertex;
            vertex.position = glm::vec3(x, height(x, z), z);
            vertex.material = terrain_material::rough;
            mesh.vertices.push_back(vertex);
        }
    }
    mesh.indices = grid_triangle_indices(size, size, 0U);
    return mesh;
}
}

TEST_CASE("open level ground sees the whole sky, a bunker's floor less of it") {
    const height_grid level = bake_ground_light(make_grid(40, 2.0f, [](float, float) { return 3.0f; })).sky_visibility;
    CHECK(std::all_of(level.heights.begin(), level.heights.end(), [](const float v) { return v == 1.0f; }));

    const height_grid bunker =
        bake_ground_light(make_grid(40, 2.0f, [](const float x, const float z) { return bowl(x, z, 40.0f, 40.0f, 6.0f, 0.9f); }))
            .sky_visibility;
    CHECK(grid_at(bunker, 20, 20) < 0.95f);
    CHECK(grid_at(bunker, 20, 20) > 0.5f);
    CHECK(grid_at(bunker, 20, 20) < grid_at(bunker, 22, 20));
    CHECK(grid_at(bunker, 2, 2) == 1.0f);

    // A valley between two banks is darker at its bottom than on the banks' tops.
    const height_grid valley =
        bake_ground_light(make_grid(40, 2.0f, [](const float x, float) { return std::min(8.0f, std::abs(x - 40.0f) * 0.4f); }))
            .sky_visibility;
    CHECK(grid_at(valley, 20, 20) < 0.85f);
    CHECK(grid_at(valley, 0, 20) == 1.0f);
}

TEST_CASE("the ground's heights land on the sky grid, gaps at its lowest") {
    const terrain_mesh slope = make_ground(11, 3.0f, [](const float x, const float z) { return 1.0f + 0.1f * x - 0.05f * z; });
    const height_grid heights = rasterize_ground_heights(slope, 2.0f);
    REQUIRE(heights.columns == 16);
    REQUIRE(heights.rows == 16);
    for (int row = 0; row < heights.rows; ++row) {
        for (int column = 0; column < heights.columns; ++column) {
            const float x = static_cast<float>(column) * 2.0f;
            const float z = static_cast<float>(row) * 2.0f;
            CHECK(near(grid_at(heights, column, row), 1.0f + 0.1f * x - 0.05f * z, 0.0001f));
        }
    }

    terrain_mesh holed = make_ground(11, 3.0f, [](float, float) { return 5.0f; });
    holed.vertices[0].position.y = 2.0f;
    holed.indices.erase(holed.indices.begin() + 60, holed.indices.begin() + 120);
    const height_grid gaps = rasterize_ground_heights(holed, 2.0f);
    CHECK(std::all_of(gaps.heights.begin(), gaps.heights.end(), [](const float v) { return v >= 2.0f && v <= 5.0f; }));
    CHECK(std::any_of(gaps.heights.begin(), gaps.heights.end(), [](const float v) { return v == 2.0f; }));

    // A huge area keeps its grid bounded by growing the spacing.
    const height_grid huge = rasterize_ground_heights(make_ground(3, 3000.0f, [](float, float) { return 0.0f; }), 2.0f);
    CHECK(huge.columns <= max_sky_grid_side);
    CHECK(huge.rows <= max_sky_grid_side);
}

TEST_CASE("the ground's render mesh carries the sky each vertex sees") {
    const terrain_mesh level = make_ground(31, 1.0f, [](float, float) { return 0.0f; });
    const render_static_mesh open =
        make_terrain_render_mesh({&level}, 1U, test_scene_colors().ground, bake_ground_light(level));
    CHECK(std::all_of(open.vertices.begin(), open.vertices.end(), [](const render_terrain_vertex& v) { return v.ao == 1.0f; }));

    const terrain_mesh bunker = make_ground(31, 1.0f, [](const float x, const float z) { return bowl(x, z, 15.0f, 15.0f, 6.0f, 0.9f); });
    const render_static_mesh bowl_mesh =
        make_terrain_render_mesh({&bunker}, 1U, test_scene_colors().ground, bake_ground_light(bunker));
    const std::size_t centre = 15U * 31U + 15U;
    CHECK(bowl_mesh.vertices[centre].ao < 0.95f);
    CHECK(bowl_mesh.vertices[0].ao == 1.0f);

    // Without a bake every vertex sees all of the sky and nothing on its horizon.
    const render_static_mesh unbaked = make_terrain_render_mesh({&bunker}, 1U, test_scene_colors().ground, ground_light_bake{});
    CHECK(unbaked.vertices[centre].ao == 1.0f);
    CHECK((unbaked.vertices[centre].horizon == std::array<std::uint8_t, 8>{}));
}

TEST_CASE("a course's ground bakes its sky and horizons within 0 and 1") {
    const game_state state = started_game(fixture_hub_course());
    const ground_light_bake light = bake_ground_light(state.area.ground);
    const height_grid& sky = light.sky_visibility;
    REQUIRE(!sky.heights.empty());
    CHECK(std::all_of(sky.heights.begin(), sky.heights.end(), [](const float v) { return v >= 0.0f && v <= 1.0f; }));
    for (const height_grid& horizon : light.horizons) {
        CHECK(horizon.columns == sky.columns);
        CHECK(horizon.rows == sky.rows);
        CHECK(std::all_of(horizon.heights.begin(), horizon.heights.end(), [](const float v) { return v >= 0.0f && v <= 1.0f; }));
    }
}

TEST_CASE("level ground has nothing on its horizon; a ridge hides the low sun from the ground behind it") {
    const ground_light_bake level = bake_ground_light(make_grid(60, 2.0f, [](float, float) { return 3.0f; }));
    for (const height_grid& horizon : level.horizons) {
        CHECK(std::all_of(horizon.heights.begin(), horizon.heights.end(), [](const float v) { return v == 0.0f; }));
    }

    // A ridge 10 m high along z at x = 80, level ground either side of it.
    const auto ridge = [](const float x, float) { return std::max(0.0f, 10.0f - std::abs(x - 80.0f)); };
    const ground_light_bake light = bake_ground_light(make_grid(80, 2.0f, ridge));
    constexpr int east = 0;  // horizon way 0 looks along +x
    constexpr int west = 4;
    const float behind = grid_at(light.horizons[east], 20, 40);  // 30 m west of the ridge's foot, looking at it
    CHECK(behind > 0.1f);
    // About the sine of atan(10 / 40): the ridge's top seen from 40 m off,
    // though no step of the march lands on it.
    CHECK(near(behind, 10.0f / std::hypot(10.0f, 40.0f), 0.05f));
    // Nearer the ridge it rises higher.
    CHECK(grid_at(light.horizons[east], 30, 40) > behind);
    // Looking away from the ridge, nothing rises.
    CHECK(grid_at(light.horizons[west], 20, 40) == 0.0f);
    // On its far side the ridge is to the west.
    CHECK(grid_at(light.horizons[west], 60, 40) > 0.1f);
    CHECK(grid_at(light.horizons[east], 60, 40) == 0.0f);

    // The render mesh carries it in bytes.
    const terrain_mesh ground = make_ground(41, 4.0f, ridge);
    const render_static_mesh mesh = make_terrain_render_mesh({&ground}, 1U, test_scene_colors().ground, bake_ground_light(ground));
    const render_terrain_vertex& foot = mesh.vertices[20U * 41U + 10U];  // x = 40
    CHECK(foot.horizon[east] > 25);
    CHECK(foot.horizon[west] == 0);
}

TEST_CASE("the sun counts on the two horizon ways either side of it") {
    const auto sum = [](const std::array<float, horizon_way_count>& weights) {
        float total = 0.0f;
        for (const float weight : weights) {
            total += weight;
        }
        return total;
    };
    const std::array<float, horizon_way_count> east = horizon_way_weights(glm::vec3(1.0f, 0.3f, 0.0f));
    CHECK(near(east[0], 1.0f, 0.0001f));
    CHECK(near(sum(east), 1.0f, 0.0001f));

    // Halfway between +x and the +x+z diagonal.
    const float angle = glm::radians(22.5f);
    const std::array<float, horizon_way_count> between = horizon_way_weights(glm::vec3(std::cos(angle), 0.5f, std::sin(angle)));
    CHECK(near(between[0], 0.5f, 0.001f));
    CHECK(near(between[1], 0.5f, 0.001f));

    // Just short of a full turn, between the last way and the first.
    const float late = glm::radians(350.0f);
    const std::array<float, horizon_way_count> wrapped = horizon_way_weights(glm::vec3(std::cos(late), 0.2f, std::sin(late)));
    CHECK(wrapped[7] + wrapped[0] > 0.999f);
    CHECK(wrapped[0] > wrapped[7]);
    CHECK(near(sum(wrapped), 1.0f, 0.0001f));

    // Straight overhead still sums to 1.
    CHECK(near(sum(horizon_way_weights(glm::vec3(0.0f, 1.0f, 0.0f))), 1.0f, 0.0001f));
}
