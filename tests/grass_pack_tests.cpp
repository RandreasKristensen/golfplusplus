#include "doctest.h"

#include "grass_oracle.h"
#include "renderer/grass_baker.h"
#include "renderer/grass_pack.h"
#include "renderer/grass_tile_backing.h"
#include "renderer/grass_tiles.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace {
constexpr std::size_t node_count = static_cast<std::size_t>(grass_tile_nodes) * grass_tile_nodes;

render_terrain_vertex vertex(const glm::vec3& position, const float blades) {
    render_terrain_vertex v;
    v.position = position;
    v.set_blades(blades);
    return v;
}

void quad(render_static_mesh& mesh, const glm::vec2& low, const glm::vec2& high, const float base, const float slope_x, const float blades) {
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const glm::vec2 p : {low, glm::vec2(high.x, low.y), high, glm::vec2(low.x, high.y)}) {
        mesh.vertices.push_back(vertex({p.x, base + slope_x * p.x, p.y}, blades));
    }
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) { mesh.indices.push_back(first + i); }
    mesh.bounds = compute_render_mesh_bounds(mesh.vertices);
}

// Overlapping ground (the later triangle decides), a heavy rough patch, bare zones
// over negative and positive tiles, a sliver and a tee.
struct course_meshes {
    render_static_mesh ground;
    render_static_mesh overlay;
    std::vector<tee_box> tees;
};

course_meshes busy_course() {
    course_meshes course;
    quad(course.ground, {-70.3f, -41.7f}, {75.1f, 38.9f}, 3.0f, 0.04f, 1.0f);
    quad(course.ground, {-20.0f, -10.0f}, {30.0f, 20.0f}, 5.5f, -0.05f, 2.0f);
    quad(course.ground, {-20.0f, 20.0f}, {30.0f, 30.0f}, 5.5f, -0.05f, 0.0f);
    quad(course.overlay, {-60.0f, -35.0f}, {-30.0f, -5.0f}, 3.0f, 0.0f, 2.0f);
    quad(course.overlay, {35.0f, -30.0f}, {60.0f, -20.0f}, 3.0f, 0.0f, 0.0f);
    const auto first = static_cast<std::uint32_t>(course.overlay.vertices.size());
    for (const glm::vec2 p : {glm::vec2(-50.0f, 20.0f), glm::vec2(40.0f, 24.0f), glm::vec2(40.0f, 24.03f)}) {
        course.overlay.vertices.push_back(vertex({p.x, 3.0f, p.y}, 0.0f));
    }
    for (const std::uint32_t i : {0u, 1u, 2u}) { course.overlay.indices.push_back(first + i); }
    course.overlay.bounds = compute_render_mesh_bounds(course.overlay.vertices);
    tee_box tee;
    tee.center = glm::vec3(-40.0f, 3.0f, 10.0f);
    tee.half_width = 1.5f;
    tee.half_length = 2.5f;
    tee.down_hole = glm::normalize(glm::vec3(1.0f, 0.0f, 0.5f));
    course.tees = {tee};
    return course;
}
}

TEST_CASE("a tile baked alone has the nodes one dense raster of the whole lattice gives") {
    const course_meshes course = busy_course();
    const dense_grass dense = bake_dense_grass(course.ground, course.overlay, course.tees);
    grass_baker baker(course.ground, course.overlay, course.tees);
    REQUIRE(baker.columns() == dense.columns);
    REQUIRE(baker.rows() == dense.rows);
    CHECK(baker.origin() == dense.origin);
    const grass_tile_extent extent = grass_tile_extent_of(dense.origin, dense.columns, dense.rows);
    CHECK(extent.counts.x >= 4);
    CHECK(extent.first_tile.x < 0);
    CHECK(extent.first_tile.y < 0);
    grass_exact_tile exact;
    int baked = 0;
    int heavy = 0;
    int tied = 0;
    for (int tz = 0; tz < extent.counts.y; ++tz) {
        for (int tx = 0; tx < extent.counts.x; ++tx) {
            const glm::ivec2 tile = extent.first_tile + glm::ivec2(tx, tz);
            const bool reached = baker.bake(tile, exact);
            baked += reached ? 1 : 0;
            for (int z = 0; z < grass_tile_nodes; ++z) {
                for (int x = 0; x < grass_tile_nodes; ++x) {
                    const glm::ivec2 lattice = tile * grass_tile_cells + glm::ivec2(x, z) - extent.first_cell;
                    const glm::ivec2 near = glm::clamp(lattice, glm::ivec2(0), glm::ivec2(dense.columns - 1, dense.rows - 1));
                    const glm::vec3 node = dense.node(near.x, near.y);
                    if (!reached) {
                        CHECK(node.z == 0.0f);
                        continue;
                    }
                    const std::size_t i = static_cast<std::size_t>(z) * grass_tile_nodes + x;
                    REQUIRE(exact.height[i] == node.x);
                    REQUIRE(exact.kind[i] == node.z);
                    // The distance differs only by the float rounding of an edge walked from its other end.
                    REQUIRE(std::abs(exact.clearance[i] - (near == lattice ? node.y : 0.0f)) <= 2.0e-5f);
                    heavy += node.z > 1.5f ? 1 : 0;
                    const glm::vec2 world = dense.origin + glm::vec2(near) * grass_field_spacing;
                    tied += (world.x > -19.0f && world.x < 29.0f && world.y > -9.0f && world.y < 19.0f &&
                             std::abs(node.x - (5.5f - 0.05f * world.x)) < 1.0e-3f) ? 1 : 0;
                }
            }
        }
    }
    CHECK(baked > 4);
    CHECK(heavy > 1000);
    // The second ground quad lies over the first and, drawn later, decides the height under it.
    CHECK(tied > 1000);
}

TEST_CASE("a baked field gives what its dense oracle gives, within the stored precision") {
    const course_meshes course = busy_course();
    const dense_grass dense = bake_dense_grass(course.ground, course.overlay, course.tees);
    const grass_field field = bake_grass_field(course.ground, course.overlay, course.tees);
    REQUIRE(field.columns == dense.columns);
    REQUIRE(field.rows == dense.rows);
    CHECK(field.backing != nullptr);
    CHECK(grass_field_memory_bytes(field) > 0);
    std::mt19937 random(5);
    std::uniform_real_distribution<float> along_x(-72.0f, 77.0f);
    std::uniform_real_distribution<float> along_z(-44.0f, 41.0f);
    int checked = 0;
    for (int i = 0; i < 60000; ++i) {
        const glm::vec2 root(along_x(random), along_z(random));
        const glm::vec3 exact = sample_dense_grass(dense, root);
        const glm::vec3 stored = sample_grass_field(field, root);
        const grass_tile_info* tile = grass_tile_at(field, grass_tile_of(root));
        if (tile == nullptr || tile->max_clearance == 0.0f) {
            CHECK(exact.y < grass_field_error + grass_field_distance_cap / 65535.0f);
            continue;
        }
        ++checked;
        CHECK(stored.y <= exact.y + 2.0e-5f);
        CHECK(exact.y - stored.y <= grass_field_distance_cap / 65535.0f + 2.0e-5f);
        if (exact.y > 0.0f) { CHECK(std::abs(stored.x - exact.x) <= 0.5f * grass_height_step + 1.0e-5f); }
    }
    CHECK(checked > 20000);
    // Bounds are of what is stored.
    grass_tile_texels texels;
    for (int tz = 0; tz < field.tile_counts.y; ++tz) {
        for (int tx = 0; tx < field.tile_counts.x; ++tx) {
            const glm::ivec2 tile = field.first_tile + glm::ivec2(tx, tz);
            REQUIRE(fill_grass_tile(field, tile, texels));
            const grass_tile_info& info = *grass_tile_at(field, tile);
            for (std::size_t i = 0; i < node_count; ++i) {
                if (decode_grass_kind(texels.kind[i]) > 0.0f && info.max_clearance > 0.0f) {
                    REQUIRE(texels.height[i] >= info.low);
                    REQUIRE(texels.height[i] <= info.high);
                }
                REQUIRE(decode_grass_clearance(texels.clearance[i]) <= info.max_clearance);
            }
        }
    }
}

TEST_CASE("packed heights are within a millimetre, and a node two tiles share decodes the same in both") {
    std::mt19937 random(21);
    std::uniform_real_distribution<float> relief(-40.0f, 60.0f);
    for (int round = 0; round < 40; ++round) {
        grass_exact_tile a;
        grass_exact_tile b;
        for (grass_exact_tile* tile : {&a, &b}) {
            tile->height.assign(node_count, 0.0f);
            tile->clearance.assign(node_count, 5.0f);
            tile->kind.assign(node_count, 1.0f);
        }
        // Tile a's last column is tile b's first, at a different range of heights.
        const float lift = relief(random) * 0.2f;
        for (int z = 0; z < grass_tile_nodes; ++z) {
            for (int x = 0; x < grass_tile_nodes; ++x) {
                const std::size_t i = static_cast<std::size_t>(z) * grass_tile_nodes + x;
                a.height[i] = relief(random) * 0.1f;
                b.height[i] = a.height[i] + lift + static_cast<float>(x) * 0.07f;
            }
            const std::size_t last = static_cast<std::size_t>(z) * grass_tile_nodes + grass_tile_cells;
            const std::size_t head = static_cast<std::size_t>(z) * grass_tile_nodes;
            b.height[head] = a.height[last];
        }
        a.clearance[7] = 0.0f;
        a.kind[9] = 0.0f;
        a.height[9] = grass_no_ground_height;
        grass_tile_info info_a;
        grass_tile_info info_b;
        const auto bytes_a = pack_grass_tile(a, info_a);
        const auto bytes_b = pack_grass_tile(b, info_b);
        REQUIRE(!bytes_a.empty());
        REQUIRE(!bytes_b.empty());
        grass_tile_texels out_a;
        grass_tile_texels out_b;
        REQUIRE(unpack_grass_tile(bytes_a.data(), bytes_a.size(), out_a));
        REQUIRE(unpack_grass_tile(bytes_b.data(), bytes_b.size(), out_b));
        for (std::size_t i = 0; i < node_count; ++i) {
            if (a.kind[i] != 0.0f) { REQUIRE(std::abs(out_a.height[i] - a.height[i]) <= 0.5f * grass_height_step + 1.0e-5f); }
            REQUIRE(std::abs(out_b.height[i] - b.height[i]) <= 0.5f * grass_height_step + 1.0e-5f);
        }
        CHECK(out_a.height[9] == grass_no_ground_height);
        CHECK(out_a.kind[9] == encode_grass_kind(0.0f));
        for (int z = 0; z < grass_tile_nodes; ++z) {
            const std::size_t last = static_cast<std::size_t>(z) * grass_tile_nodes + grass_tile_cells;
            const std::size_t head = static_cast<std::size_t>(z) * grass_tile_nodes;
            REQUIRE(out_a.height[last] == out_b.height[head]);
        }
        CHECK(info_a.low <= info_a.high);
        for (std::size_t i = 0; i < node_count; ++i) {
            if (a.kind[i] != 0.0f) { REQUIRE(out_a.height[i] >= info_a.low); REQUIRE(out_a.height[i] <= info_a.high); }
        }
    }
}

TEST_CASE("a tile whose relief does not fit the step keeps float heights, and one that cannot grow is not kept") {
    grass_exact_tile steep;
    steep.height.assign(node_count, 10.0f);
    steep.clearance.assign(node_count, 4.0f);
    steep.kind.assign(node_count, 2.0f);
    steep.height[100] = 10.0f + 66.0f;
    steep.height[200] = 10.1234567f;
    grass_tile_info info;
    const auto bytes = pack_grass_tile(steep, info);
    REQUIRE(!bytes.empty());
    grass_tile_texels out;
    REQUIRE(unpack_grass_tile(bytes.data(), bytes.size(), out));
    CHECK(out.height[100] == 76.0f);
    CHECK(out.height[200] == steep.height[200]);
    CHECK(info.high == 76.0f);
    // The same with 65.5 m of relief fits 16 bits and is stored smaller.
    steep.height[100] = 10.0f + 65.0f;
    grass_tile_info small;
    const auto fitted = pack_grass_tile(steep, small);
    CHECK(fitted.size() < bytes.size());
    CHECK(std::abs(unpack_grass_tile(fitted.data(), fitted.size(), out) ? out.height[100] - 75.0f : 1.0f) <= grass_height_step);

    grass_exact_tile thin = steep;
    thin.clearance.assign(node_count, grass_field_error - 0.01f);
    grass_tile_info none;
    none.max_clearance = 3.0f;
    CHECK(pack_grass_tile(thin, none).empty());
    CHECK(none.max_clearance == 0.0f);
    CHECK(none.low == 0.0f);
    CHECK(none.high == 0.0f);
}

TEST_CASE("a tile of one clearance and one kind is stored in two bytes a node, and damaged bytes are refused") {
    grass_exact_tile flat;
    flat.height.assign(node_count, 3.0f);
    flat.clearance.assign(node_count, grass_field_distance_cap);
    flat.kind.assign(node_count, 1.0f);
    grass_tile_info info;
    auto bytes = pack_grass_tile(flat, info);
    CHECK(bytes.size() == 8 + node_count * 2);
    CHECK(info.max_clearance == grass_field_distance_cap);
    grass_tile_texels out;
    REQUIRE(unpack_grass_tile(bytes.data(), bytes.size(), out));
    CHECK(out.clearance[node_count - 1] == 65535);
    CHECK(out.kind[5] == encode_grass_kind(1.0f));
    // Varying values are stored in full.
    flat.clearance[3] = 10.0f;
    flat.kind[4] = 2.0f;
    CHECK(pack_grass_tile(flat, info).size() == 8 + node_count * 5);

    CHECK(!unpack_grass_tile(bytes.data(), bytes.size() - 1, out));
    CHECK(!unpack_grass_tile(bytes.data(), 3, out));
    auto longer = bytes;
    longer.push_back(0);
    CHECK(!unpack_grass_tile(longer.data(), longer.size(), out));
    auto flagged = bytes;
    flagged[0] = 0x80;
    CHECK(!unpack_grass_tile(flagged.data(), flagged.size(), out));
}

TEST_CASE("the tile backing returns what was put, by slot, and nothing for a slot left empty") {
    grass_memory_backing backing(4);
    backing.put(1, {1, 2, 3});
    backing.put(3, {9, 8});
    std::vector<std::uint8_t> out;
    CHECK(!backing.read(0, out));
    REQUIRE(backing.read(1, out));
    CHECK((out == std::vector<std::uint8_t>{1, 2, 3}));
    REQUIRE(backing.read(3, out));
    CHECK((out == std::vector<std::uint8_t>{9, 8}));
    CHECK(!backing.read(2, out));
    CHECK(!backing.read(4, out));
    CHECK(backing.memory_bytes() >= 5);
}
