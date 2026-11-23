#include "doctest.h"

#include "renderer/grass_tile_cache.h"
#include "renderer/grass_tile_store.h"
#include "renderer/grass_tiles.h"
#include "grass_oracle.h"
#include "renderer/grass_pack.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float step = grass_field_distance_cap / 65535.0f;

render_static_mesh ground_between(const glm::vec2& low, const glm::vec2& high) {
    render_static_mesh mesh;
    for (const glm::vec2 p : {low, glm::vec2(high.x, low.y), high, glm::vec2(low.x, high.y)}) {
        render_terrain_vertex v;
        v.position = glm::vec3(p.x, 3.0f + 0.07f * p.x - 0.03f * p.y, p.y);
        v.set_blades(1.0f);
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.bounds = compute_render_mesh_bounds(mesh.vertices);
    mesh.revision = 5;
    return mesh;
}

// Ground over negative and positive tiles, a bare sliver, a bare square and a tee:
// the baked field and the full-precision oracle of the same meshes.
struct spread {
    grass_field field;
    dense_grass dense;
};

spread spread_field() {
    const render_static_mesh ground = ground_between({-45.3f, -70.2f}, {40.1f, 30.7f});
    render_static_mesh bare;
    for (const glm::vec2 p : {glm::vec2(-30, -50), glm::vec2(20, 10), glm::vec2(20, 10.03f), glm::vec2(2, 2), glm::vec2(12, 2), glm::vec2(12, 9), glm::vec2(2, 9)}) {
        render_terrain_vertex v;
        v.position = glm::vec3(p.x, 3.0f, p.y);
        bare.vertices.push_back(v);
    }
    bare.indices = {0, 1, 2, 3, 4, 5, 3, 5, 6};
    tee_box tee;
    tee.center = glm::vec3(-20.0f, 3.0f, -10.0f);
    tee.half_width = 1.0f;
    tee.half_length = 2.0f;
    tee.down_hole = glm::vec3(1.0f, 0.0f, 0.0f);
    return {bake_grass_field(ground, bare, {tee}), bake_dense_grass(ground, bare, {tee})};
}

grass_tile_texels texels_of(const grass_field& field, const glm::ivec2& tile) {
    grass_tile_texels texels;
    fill_grass_tile(field, tile, texels);
    return texels;
}
}

TEST_CASE("grass clearance is rounded down, never up, and kind round-trips exactly") {
    std::mt19937 random(11);
    std::uniform_real_distribution<float> metres(0.0f, grass_field_distance_cap);
    for (int i = 0; i < 200000; ++i) {
        const float value = metres(random);
        const float back = decode_grass_clearance(encode_grass_clearance(value));
        // The decode's own float rounding is far inside the margin the field error keeps.
        CHECK(back <= value + 4.0e-6f);
        CHECK(value - back <= step + 4.0e-6f);
    }
    for (const float exact : {0.0f, 1.0f, 2.0f, 32.0f}) {
        CHECK(decode_grass_clearance(encode_grass_clearance(exact)) <= exact + 4.0e-6f);
    }
    CHECK(encode_grass_clearance(-3.0f) == 0);
    CHECK(encode_grass_clearance(99.0f) == 65535);
    for (const float kind : {0.0f, 1.0f, 2.0f}) { CHECK(decode_grass_kind(encode_grass_kind(kind)) == kind); }
    CHECK(encode_grass_kind(0.0f) == -127);
    // The margin: the half-cell diagonal bound sits under grass_field_error by more
    // than quantizing and interpolating in float can spend.
    CHECK(grass_field_error - 0.25f * std::sqrt(2.0f) > 4.0e-4f);
}

TEST_CASE("tile texels equal the field nodes, and neighbours share their border nodes exactly") {
    const auto [field, dense] = spread_field();
    REQUIRE(!field.tiles.empty());
    CHECK(field.first_tile.x <= -2);
    CHECK(field.first_tile.y <= -3);
    const glm::ivec2 first_cell = glm::ivec2(glm::round(field.origin / grass_field_spacing));
    int stored = 0;
    for (int tz = 0; tz < field.tile_counts.y; ++tz) {
        for (int tx = 0; tx < field.tile_counts.x; ++tx) {
            const glm::ivec2 tile = field.first_tile + glm::ivec2(tx, tz);
            const grass_tile_texels here = texels_of(field, tile);
            const bool kept = grass_tile_at(field, tile)->max_clearance > 0.0f;
            stored += kept ? 1 : 0;
            for (int z = 0; z < grass_tile_nodes; ++z) {
                for (int x = 0; x < grass_tile_nodes; ++x) {
                    const glm::ivec2 cell = tile * grass_tile_cells + glm::ivec2(x, z);
                    const glm::ivec2 at = cell - first_cell;
                    const std::size_t i = static_cast<std::size_t>(z) * grass_tile_nodes + x;
                    if (!kept) {
                        CHECK(here.clearance[i] == 0);
                    } else if (at.x >= 0 && at.y >= 0 && at.x < field.columns && at.y < field.rows) {
                        const glm::vec3 node = dense.node(at.x, at.y);
                        // A node with no ground keeps the one height meant for it; the others are on the global step.
                        if (node.z == 0.0f) { CHECK(here.height[i] == grass_no_ground_height); }
                        else { CHECK(std::abs(here.height[i] - node.x) <= 0.5f * grass_height_step + 1.0e-5f); }
                        CHECK(decode_grass_clearance(here.clearance[i]) <= node.y + 4.0e-6f);
                        CHECK(node.y - decode_grass_clearance(here.clearance[i]) <= step + 4.0e-6f);
                        // Whole kinds are exact; an interpolated node keeps within a 127th.
                        CHECK(std::abs(decode_grass_kind(here.kind[i]) - node.z) <= 1.0f / 127.0f);
                    } else {
                        CHECK(here.clearance[i] == 0);
                    }
                }
            }
            // A node two tiles hold decodes the same in both.
            if (tx + 1 < field.tile_counts.x && kept) {
                const glm::ivec2 other = tile + glm::ivec2(1, 0);
                const grass_tile_texels next = texels_of(field, other);
                if (grass_tile_at(field, other)->max_clearance > 0.0f) {
                    for (int z = 0; z < grass_tile_nodes; ++z) {
                        const std::size_t last = static_cast<std::size_t>(z) * grass_tile_nodes + grass_tile_cells;
                        const std::size_t head = static_cast<std::size_t>(z) * grass_tile_nodes;
                        CHECK(here.height[last] == next.height[head]);
                        CHECK(here.clearance[last] == next.clearance[head]);
                        CHECK(here.kind[last] == next.kind[head]);
                    }
                }
            }
            if (tz + 1 < field.tile_counts.y && kept) {
                const glm::ivec2 other = tile + glm::ivec2(0, 1);
                const grass_tile_texels next = texels_of(field, other);
                if (grass_tile_at(field, other)->max_clearance > 0.0f) {
                    for (int x = 0; x < grass_tile_nodes; ++x) {
                        const std::size_t last = static_cast<std::size_t>(grass_tile_cells) * grass_tile_nodes + x;
                        CHECK(here.height[last] == next.height[x]);
                        CHECK(here.clearance[last] == next.clearance[x]);
                    }
                }
            }
        }
    }
    CHECK(stored > 4);
}

TEST_CASE("every bilinear tap of a tile agrees with the full-precision field, at tile corners and edges too") {
    const auto [field, dense] = spread_field();
    std::mt19937 random(3);
    std::uniform_real_distribution<float> along_x(-48.0f, 43.0f);
    std::uniform_real_distribution<float> along_z(-73.0f, 33.0f);
    std::vector<glm::vec2> roots;
    for (int i = 0; i < 40000; ++i) { roots.emplace_back(along_x(random), along_z(random)); }
    // Every tile corner and edge, a hair either side, and the cell nodes themselves.
    for (int tz = field.first_tile.y; tz <= field.first_tile.y + field.tile_counts.y; ++tz) {
        for (int tx = field.first_tile.x; tx <= field.first_tile.x + field.tile_counts.x; ++tx) {
            const glm::vec2 corner = glm::vec2(tx, tz) * grass_tile_meters;
            for (const glm::vec2 d : {glm::vec2(0), glm::vec2(1.0e-4f, 0), glm::vec2(0, 1.0e-4f), glm::vec2(-1.0e-4f, -1.0e-4f),
                                      glm::vec2(0.25f, 0), glm::vec2(0, 0.25f), glm::vec2(0.5f, 0.5f)}) {
                roots.push_back(corner + d);
            }
        }
    }
    std::map<std::pair<int, int>, grass_tile_texels> loaded;
    float worst_height = 0.0f;
    for (const glm::vec2 root : roots) {
        const glm::ivec2 tile = grass_tile_of(root);
        auto at = loaded.find({tile.x, tile.y});
        if (at == loaded.end()) { at = loaded.emplace(std::make_pair(tile.x, tile.y), texels_of(field, tile)).first; }
        const grass_tile_texels& texels = at->second;
        const glm::vec3 oracle = sample_dense_grass(dense, root);
        const glm::vec3 tapped = sample_grass_tile(texels, tile, root);
        const grass_tile_info* info = grass_tile_at(field, tile);
        if (info == nullptr || info->max_clearance == 0.0f) {
            // A tile no blade can grow in is not kept; the exact field agrees none is accepted.
            CHECK(oracle.y < grass_field_error + step);
            continue;
        }
        // Never more clearance than the exact field, and within the step of it.
        CHECK(tapped.y <= oracle.y + 2.0e-5f);
        CHECK(oracle.y - tapped.y <= step + 2.0e-5f);
        if (oracle.y > 0.0f) {
            worst_height = std::max(worst_height, std::abs(tapped.x - oracle.x));
            CHECK(std::abs(tapped.z - oracle.z) <= 2.0e-5f);
        }
    }
    CHECK(worst_height <= 0.5f * grass_height_step + 1.0e-5f);
}

TEST_CASE("a tile never reads past its own nodes, and continues into its neighbour without a seam") {
    const grass_field field = spread_field().field;
    const glm::ivec2 a(-1, -1);
    const glm::ivec2 b(0, -1);
    const grass_tile_texels in_a = texels_of(field, a);
    const grass_tile_texels in_b = texels_of(field, b);
    // Outside its 64 cells there is nothing to tap.
    CHECK(sample_grass_tile(in_a, a, glm::vec2(0.0f, -16.0f)) == glm::vec3(0.0f));
    CHECK(sample_grass_tile(in_a, a, glm::vec2(-40.0f, -16.0f)) == glm::vec3(0.0f));
    for (float z = -31.9f; z < -0.1f; z += 0.37f) {
        const glm::vec3 left = sample_grass_tile(in_a, a, glm::vec2(-0.0005f, z));
        const glm::vec3 right = sample_grass_tile(in_b, b, glm::vec2(0.0f, z));
        CHECK(std::abs(left.x - right.x) < 0.001f);
        CHECK(std::abs(left.y - right.y) < 0.001f);
    }
}

TEST_CASE("tile bounds hold every node, and tiles with no clearance say so") {
    const grass_field field = spread_field().field;
    int empty = 0;
    int grown = 0;
    for (int tz = 0; tz < field.tile_counts.y; ++tz) {
        for (int tx = 0; tx < field.tile_counts.x; ++tx) {
            const glm::ivec2 tile = field.first_tile + glm::ivec2(tx, tz);
            const grass_tile_info* info = grass_tile_at(field, tile);
            REQUIRE(info != nullptr);
            const grass_tile_texels texels = texels_of(field, tile);
            float most = 0.0f;
            for (std::size_t i = 0; i < texels.height.size(); ++i) {
                // Nodes with no ground (kind none) bound nothing: no blade grows over them.
                if (decode_grass_kind(texels.kind[i]) > 0.0f) {
                    CHECK(texels.height[i] >= info->low);
                    CHECK(texels.height[i] <= info->high);
                }
                most = std::max(most, decode_grass_clearance(texels.clearance[i]));
            }
            CHECK(most <= info->max_clearance + 4.0e-6f);
            (info->max_clearance > 0.0f ? grown : empty) += 1;
        }
    }
    CHECK(grown > 0);
    CHECK(grass_tile_at(field, field.first_tile - glm::ivec2(1, 0)) == nullptr);
    CHECK(grass_tile_at(field, field.first_tile + field.tile_counts) == nullptr);
    // A tile wholly inside a bare zone has no clearance at all.
    render_static_mesh bare;
    for (const glm::vec2 p : {glm::vec2(-100, -100), glm::vec2(100, -100), glm::vec2(100, 100), glm::vec2(-100, 100)}) {
        render_terrain_vertex v;
        v.position = glm::vec3(p.x, 3.0f, p.y);
        bare.vertices.push_back(v);
    }
    bare.indices = {0, 1, 2, 0, 2, 3};
    const grass_field all_bare = bake_grass_field(ground_between({-45.3f, -70.2f}, {40.1f, 30.7f}), bare, {});
    for (const grass_tile_info& info : all_bare.tiles) { CHECK(info.max_clearance == 0.0f); }
}

TEST_CASE("an accepted root is never nearer bare ground than a blade reaches, whatever the quantization") {
    const grass_field field = spread_field().field;
    const material_zone sliver = make_material_zone(material_zone_type::fairway, {{-30, -50}, {20, 10}, {20, 10.03f}});
    const material_zone square = make_material_zone(material_zone_type::fairway, {{2, 2}, {12, 2}, {12, 9}, {2, 9}});
    grass_style style;
    style.blade_height_meters = 0.09f;
    style.blade_width_meters = 0.025f;
    style.heavy_blade_share = 2.2f;
    const float reach = grass_blade_reach(style, 4.0f);
    glm::ivec2 loaded_tile(1 << 20);
    grass_tile_texels texels;
    int accepted = 0;
    for (float x = -44.0f; x < 39.0f; x += 0.173f) {
        for (float z = -69.0f; z < 29.0f; z += 0.191f) {
            const glm::vec2 root(x, z);
            const glm::ivec2 tile = grass_tile_of(root);
            if (loaded_tile != tile) { fill_grass_tile(field, tile, texels); loaded_tile = tile; }
            if (sample_grass_tile(texels, tile, root).y >= reach + grass_field_error) {
                ++accepted;
                const float exact = std::min(zone_distance_outside(sliver, {x, 0, z}), zone_distance_outside(square, {x, 0, z}));
                CHECK(exact >= reach);
            }
        }
    }
    CHECK(accepted > 1000);
}

TEST_CASE("tile residency keeps unique layers, evicts the least recent, and never leaves a stale mapping") {
    grass_tile_cache cache(4);
    const auto ids = [](const std::vector<grass_tile_cache::upload>& uploads) {
        std::set<int> layers;
        for (const auto& u : uploads) { layers.insert(u.layer); }
        return layers;
    };
    const auto first = cache.make_resident({{0, 0}, {1, 0}, {2, 0}});
    CHECK(first.uploads.size() == 3);
    CHECK(first.evicted.empty());
    CHECK(ids(first.uploads).size() == 3);
    // Needing a, then d and e: the room is found by evicting b (the oldest).
    cache.make_resident({{1, 0}});
    cache.make_resident({{2, 0}});
    cache.make_resident({{0, 0}});
    cache.make_resident({{2, 0}});
    const auto second = cache.make_resident({{3, 0}, {4, 0}, {0, 0}});
    CHECK(second.uploads.size() == 2);
    REQUIRE(second.evicted.size() == 1);
    CHECK(second.evicted[0] == glm::ivec2(1, 0));
    CHECK(cache.layer_of({1, 0}) == -1);
    CHECK(cache.layer_of({0, 0}) >= 0);
    // The evicted tile comes back as a miss with a layer of its own, never one still held.
    const auto third = cache.make_resident({{1, 0}, {3, 0}, {4, 0}, {0, 0}});
    CHECK(third.uploads.size() == 1);
    std::set<int> held;
    for (const glm::ivec2 tile : {glm::ivec2(0, 0), glm::ivec2(1, 0), glm::ivec2(3, 0), glm::ivec2(4, 0)}) {
        const int layer = cache.layer_of(tile);
        CHECK(layer >= 0);
        CHECK(held.insert(layer).second);
    }
    CHECK(cache.layer_of({2, 0}) == -1);
}

TEST_CASE("random walks through the cache keep it consistent and fully resident for what was asked") {
    std::mt19937 random(9);
    grass_tile_cache cache(12);
    std::set<std::pair<int, int>> mirror;
    for (int step_index = 0; step_index < 3000; ++step_index) {
        std::set<std::pair<int, int>> asked;
        const int count = 1 + static_cast<int>(random() % 12);
        const int base = static_cast<int>(random() % 30) - 15;
        while (static_cast<int>(asked.size()) < count) { asked.insert({base + static_cast<int>(random() % 6), static_cast<int>(random() % 4) - 2}); }
        std::vector<glm::ivec2> needed;
        for (const auto& [x, z] : asked) { needed.emplace_back(x, z); }
        const auto change = cache.make_resident(needed);
        for (const glm::ivec2 gone : change.evicted) { mirror.erase({gone.x, gone.y}); }
        for (const auto& up : change.uploads) { mirror.insert({up.tile.x, up.tile.y}); }
        std::set<int> layers;
        for (const auto& [x, z] : mirror) {
            const int layer = cache.layer_of({x, z});
            REQUIRE(layer >= 0);
            REQUIRE(layer < 12);
            REQUIRE(layers.insert(layer).second);
        }
        REQUIRE(static_cast<int>(mirror.size()) == cache.resident());
        for (const glm::ivec2 tile : needed) { REQUIRE(cache.layer_of(tile) >= 0); }
    }
}

TEST_CASE("the tile cache is sized by its byte budget and the driver, never by the tuning in use") {
    const std::uint64_t per_layer = static_cast<std::uint64_t>(grass_tile_nodes) * grass_tile_nodes * grass_tile_texel_bytes;
    const int layers = grass_tile_layers(2048);
    CHECK(layers > 0);
    CHECK(static_cast<std::uint64_t>(layers) * per_layer <= grass_tile_budget_bytes);
    CHECK(static_cast<std::uint64_t>(layers + 1) * per_layer > grass_tile_budget_bytes);
    CHECK(grass_tile_layers(100) == 100);
    CHECK(grass_tile_layers(0) == 0);
    // Shipped reach (22 m, three rings: 88 m) fits several times over; the largest
    // accepted tuning (100 m, four rings) cannot, and is drawn in batches.
    const auto tiles_for = [](const float reach) {
        const int across = static_cast<int>(std::ceil(2.0f * reach / grass_tile_meters)) + 1;
        return across * across;
    };
    CHECK(tiles_for(88.0f) * 4 <= layers);
    CHECK(tiles_for(800.0f) > layers);
}

TEST_CASE("no grass grows over a cell with a node that has no ground, and tile bounds ignore such nodes") {
    // Half of the square has no triangle: the diagonal is a hole edge.
    render_static_mesh half = ground_between({-12.0f, -12.0f}, {12.0f, 12.0f});
    half.indices = {0, 1, 2};
    const grass_field field = bake_grass_field(half, render_static_mesh{}, {});
    const dense_grass dense = bake_dense_grass(half, render_static_mesh{}, {});
    REQUIRE(!field.tiles.empty());
    const glm::ivec2 first_cell = glm::ivec2(glm::round(field.origin / grass_field_spacing));
    std::map<std::pair<int, int>, grass_tile_texels> loaded;
    int mixed = 0;
    int grown = 0;
    const auto tile_of_cell = [](const int c) { return c >= 0 ? c / grass_tile_cells : -((-c + grass_tile_cells - 1) / grass_tile_cells); };
    for (int z = 0; z + 1 < dense.rows; ++z) {
        for (int x = 0; x + 1 < dense.columns; ++x) {
            const bool grounded = dense.node(x, z).z != 0.0f && dense.node(x + 1, z).z != 0.0f && dense.node(x, z + 1).z != 0.0f &&
                                  dense.node(x + 1, z + 1).z != 0.0f;
            const glm::ivec2 cell = first_cell + glm::ivec2(x, z);
            const glm::ivec2 tile(tile_of_cell(cell.x), tile_of_cell(cell.y));
            auto at = loaded.find({tile.x, tile.y});
            if (at == loaded.end()) { at = loaded.emplace(std::make_pair(tile.x, tile.y), texels_of(field, tile)).first; }
            const glm::ivec2 local = cell - tile * grass_tile_cells;
            const std::size_t i = static_cast<std::size_t>(local.y) * grass_tile_nodes + local.x;
            const std::size_t nodes[4] = {i, i + 1, i + grass_tile_nodes, i + grass_tile_nodes + 1};
            if (!grounded) {
                ++mixed;
                for (const std::size_t node : nodes) { CHECK(at->second.clearance[node] == 0); }
            } else {
                ++grown;
            }
        }
    }
    CHECK(mixed > 100);
    CHECK(grown > 100);
    for (const grass_tile_info& info : field.tiles) {
        CHECK(info.low > -100.0f);
        CHECK(info.high < 100.0f);
    }
}

TEST_CASE("tiles fetched ahead take free layers or old ones, never a layer a recent draw used") {
    grass_tile_cache cache(6);
    cache.make_resident({{0, 0}, {1, 0}, {2, 0}, {3, 0}});
    // Two free layers: a budget of five fetches only what they hold, nearest first.
    auto ahead = cache.fill_ahead({{0, 0}, {10, 0}, {11, 0}, {12, 0}}, 5);
    REQUIRE(ahead.uploads.size() == 2);
    CHECK(ahead.uploads[0].tile == glm::ivec2(10, 0));
    CHECK(ahead.uploads[1].tile == glm::ivec2(11, 0));
    CHECK(ahead.evicted.empty());
    CHECK(cache.layer_of({12, 0}) == -1);
    // Every layer was used by the latest draw or just fetched: nothing may be taken.
    ahead = cache.fill_ahead({{12, 0}, {13, 0}}, 5);
    CHECK(ahead.uploads.empty());
    CHECK(ahead.evicted.empty());
    // Many draws later, a tile nobody needed is old enough, and a budget bounds the work.
    for (std::uint64_t i = 0; i < grass_tile_rotation_guard; ++i) { cache.make_resident({{0, 0}, {1, 0}}); }
    ahead = cache.fill_ahead({{12, 0}, {13, 0}, {14, 0}, {0, 0}}, 2);
    CHECK(ahead.uploads.size() == 2);
    CHECK(ahead.evicted.size() == 2);
    for (const glm::ivec2 gone : ahead.evicted) { CHECK((gone != glm::ivec2(0, 0) && gone != glm::ivec2(1, 0))); }
    CHECK(cache.layer_of({0, 0}) >= 0);
    CHECK(cache.layer_of({1, 0}) >= 0);
    CHECK(cache.layer_of({14, 0}) == -1);
    std::set<int> layers;
    for (const glm::ivec2 tile : {glm::ivec2(0, 0), glm::ivec2(1, 0), glm::ivec2(12, 0), glm::ivec2(13, 0)}) {
        const int layer = cache.layer_of(tile);
        CHECK(layer >= 0);
        layers.insert(layer);
    }
    CHECK(layers.size() == 4);
    // A draw may still take what fetching ahead may not.
    const auto draw = cache.make_resident({{40, 0}, {41, 0}, {42, 0}, {43, 0}, {44, 0}, {45, 0}});
    CHECK(draw.uploads.size() == 6);
    // A tile that could not be read gives its layer back, and is asked for as a miss again.
    cache.forget({40, 0});
    CHECK(cache.layer_of({40, 0}) == -1);
    const auto again = cache.make_resident({{40, 0}, {41, 0}});
    REQUIRE(again.uploads.size() == 1);
    CHECK(again.uploads[0].tile == glm::ivec2(40, 0));
    CHECK(again.evicted.empty());
}

TEST_CASE("the tiles ahead of the eye are those every ring may need, nearest first, within the field and bounded") {
    const grass_field field = spread_field().field;
    grass_style style;
    style.blade_height_meters = 0.09f;
    style.blade_width_meters = 0.025f;
    style.heavy_blade_share = 2.2f;
    style.reach_meters = 22.0f;
    style.tuft_spacing_meters = 0.16f;
    style.rings = 3.0f;
    const glm::vec3 eye(-5.0f, 3.0f, -20.0f);
    const auto all = grass_tiles_ahead(field, eye, style, 1000);
    REQUIRE(all.size() > 9);
    const float required = grass_blade_reach(style, 4.0f) + grass_field_error;
    float last = -1.0f;
    for (const glm::ivec2 tile : all) {
        const grass_tile_info* info = grass_tile_at(field, tile);
        REQUIRE(info != nullptr);
        CHECK(info->max_clearance >= required);
        const glm::vec2 delta = (glm::vec2(tile) + 0.5f) * grass_tile_meters - glm::vec2(eye.x, eye.z);
        CHECK(glm::dot(delta, delta) >= last);
        last = glm::dot(delta, delta);
    }
    // Wherever the eye turns: tiles on every side of the one it stands in.
    const glm::ivec2 here = grass_tile_of(glm::vec2(eye.x, eye.z));
    for (const glm::ivec2 side : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
        CHECK(std::find(all.begin(), all.end(), here + side) != all.end());
    }
    CHECK(all.front() == here);
    const auto few = grass_tiles_ahead(field, eye, style, 5);
    REQUIRE(few.size() == 5);
    CHECK(std::equal(few.begin(), few.end(), all.begin()));
    CHECK(grass_tiles_ahead(field, eye, style, 0).empty());
    CHECK(grass_tiles_ahead(grass_field{}, eye, style, 5).empty());
    // Far from the field there is nothing to fetch.
    CHECK(grass_tiles_ahead(field, glm::vec3(5000.0f, 0.0f, 5000.0f), style, 50).empty());
}
