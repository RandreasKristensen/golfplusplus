#include "doctest.h"

#include "renderer/grass_field.h"
#include "renderer/grass_tiles.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

TEST_CASE("the tufts stand on a world-fixed grid round the eye") {
    const float spacing = 0.16f;
    const grass_grid here = grass_grid_around(glm::vec3(10.03f, 0.0f, 4.51f), 20.0f, spacing);
    const grass_grid moved = grass_grid_around(glm::vec3(10.11f, 0.0f, 4.57f), 20.0f, spacing);
    CHECK(here.per_row * spacing >= 40.0f);
    CHECK(here.per_row % grass_block_tufts == 0);
    // Moving a little keeps the cells where they were in the world.
    const glm::vec2 offset = (moved.origin - here.origin) / spacing;
    CHECK(near(offset.x, std::round(offset.x)));
    CHECK(near(offset.y, std::round(offset.y)));
    CHECK(here.origin.x <= 10.03f - 20.0f);
}

TEST_CASE("a tuft is three tapering blades, each standing from its root to its tip") {
    const std::vector<glm::vec3> tuft = grass_tuft_vertices(3);
    CHECK(tuft.size() == 27U);
    int tips = 0;
    for (const glm::vec3& v : tuft) {
        CHECK(v.y >= 0.0f);
        CHECK(v.y <= 1.0f);
        tips += v.y == 1.0f ? 1 : 0;
        CHECK((v.y < 1.0f || v.x == 0.0f));
    }
    CHECK(tips == 3);
}

namespace {
render_static_mesh flat_ground() {
    render_static_mesh mesh;
    for (const glm::vec2 p : {glm::vec2(-12,-12), glm::vec2(12,-12), glm::vec2(12,12), glm::vec2(-12,12)}) {
        render_terrain_vertex v; v.position = glm::vec3(p.x, 3 + 0.1f*p.x, p.y); v.set_blades(1.0f);
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0,1,2,0,2,3};
    mesh.bounds = compute_render_mesh_bounds(mesh.vertices);
    mesh.revision = 17;
    return mesh;
}
}

TEST_CASE("only the blocks of tufts the eye may see over ground are drawn") {
    // Flat ground from x -100 to 100 and z -100 to 40; the eye looks along +z.
    render_static_mesh ground;
    for (const glm::vec2 p : {glm::vec2(-100.0f, -100.0f), glm::vec2(100.0f, -100.0f), glm::vec2(100.0f, 40.0f), glm::vec2(-100.0f, 40.0f)}) {
        render_terrain_vertex v;
        v.position = glm::vec3(p.x, 0.0f, p.y);
        v.set_blades(1.0f);
        ground.vertices.push_back(v);
    }
    ground.indices = {0, 1, 2, 0, 2, 3};
    ground.bounds = compute_render_mesh_bounds(ground.vertices);
    const grass_field field = bake_grass_field(ground, render_static_mesh{}, {});
    const glm::vec3 eye(0.0f, 1.7f, 0.0f);
    const view_frustum frustum = make_view_frustum(glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f) *
                                                   glm::lookAt(eye, eye + glm::vec3(0.0f, -0.1f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    const grass_grid grid = grass_grid_around(eye, 60.0f, 0.5f);
    const float side = grid.spacing * static_cast<float>(grass_block_tufts);
    const int blocks = grid.per_row / grass_block_tufts;
    std::size_t scratch = 0;
    const auto seen = grass_visible_blocks(grid, frustum, field, 0.3f, 1.0f, &scratch);
    CHECK(scratch >= seen.corners.size() * sizeof(glm::vec2));
    CHECK(scratch < 16 * 1024);
    CHECK(seen.tiles.size() == seen.corners.size());
    CHECK(!seen.corners.empty());
    CHECK(static_cast<int>(seen.corners.size()) < blocks * blocks / 2);
    bool ahead = false;
    for (std::size_t i = 0; i < seen.corners.size(); ++i) {
        const glm::vec2 corner = seen.corners[i];
        // Not behind the eye, and not past the ground's end.
        CHECK(corner.y + side + grid.spacing > eye.z);
        // Whole tiles select: a block may reach the ground-less rest of the last tile over it.
        CHECK(seen.tiles[i].y <= grass_tile_of(glm::vec2(0.0f, 40.0f)).y);
        ahead = ahead || (corner.x <= 0.0f && corner.x + side > 0.0f && corner.y > 10.0f);
        // Its tiles hold the block's whole reach.
        CHECK(seen.tiles[i].x == grass_tile_of(corner).x);
        CHECK(seen.tiles[i].z == grass_tile_of(corner + glm::vec2(side + grid.spacing)).x);
    }
    CHECK(ahead);
    grass_field none;
    CHECK(grass_visible_blocks(grid, frustum, none, 0.3f, 1.0f).corners.empty());
    // Tiles that cannot grow the widest blade leave nothing to draw.
    CHECK(grass_visible_blocks(grid, frustum, field, 0.3f, 1000.0f).corners.empty());
}

TEST_CASE("grass field edges are conservative at every interpolation alignment, including thin slivers and tee tops") {
    const auto ground = flat_ground();
    render_static_mesh bare;
    // A diagonal bare sliver much narrower than a field cell.
    for (const glm::vec2 p : {glm::vec2(-8,-7), glm::vec2(8,7), glm::vec2(8,7.03f)}) {
        render_terrain_vertex v; v.position = glm::vec3(p.x, 3, p.y); bare.vertices.push_back(v);
    }
    bare.indices = {0,1,2};
    tee_box tee; tee.center = glm::vec3(-5,3,5); tee.half_width = 1; tee.half_length = 2;
    tee.down_hole = glm::normalize(glm::vec3(1,0,1));
    const auto field = bake_grass_field(ground, bare, {tee});
    REQUIRE(!field.tiles.empty());
    CHECK(field.revision == ground.revision);
    for (float t = 0; t < 1; t += 0.007f) {
        const glm::vec2 p = glm::mix(glm::vec2(-8,-7), glm::vec2(8,7.01f), t);
        CHECK(sample_grass_field(field,p).y <= grass_field_error + 0.001f);
    }
    CHECK(sample_grass_field(field, {-5,5}).y == 0);
    CHECK(sample_grass_field(field, {5,-5}).y > 2);
    CHECK(near(sample_grass_field(field,{5,-5}).x, 3.5f));
    CHECK(sample_grass_field(field, {100,100}).y == 0);
    // At all subcell alignments an accepted blade has its full reach clear.
    const material_zone sliver = make_material_zone(material_zone_type::fairway, {{-8,-7},{8,7},{8,7.03f}});
    for (float x = -9; x < 9; x += 0.137f) {
        for (float z = -9; z < 9; z += 0.171f) {
            const float distance = sample_grass_field(field,{x,z}).y;
            const float exact = zone_distance_outside(sliver, {x,0,z});
            CHECK(distance <= exact + grass_field_error + 0.002f);
        }
    }
}

TEST_CASE("coarse grass representatives are nested members of the finest world grid, including negative blocks") {
    for (int z = -12; z < 12; ++z) {
        for (int x = -12; x < 12; ++x) {
            for (int level = 1; level < grass_max_rings; ++level) {
                const glm::ivec2 block(x,z);
                const auto parent = grass_representative(block,level);
                for (int dz = 0; dz < (1<<level); ++dz) {
                    for (int dx = 0; dx < (1<<level); ++dx) {
                        const auto cell=block*(1<<level)+glm::ivec2(dx,dz);
                        CHECK(grass_cell_rank(parent.x,parent.y)<=grass_cell_rank(cell.x,cell.y));
                    }
                }
                bool child = false;
                for (int dz = 0; dz < 2; ++dz) {
                    for (int dx = 0; dx < 2; ++dx) {
                        child = child || parent == grass_representative(block*2 + glm::ivec2(dx,dz),level-1);
                    }
                }
                CHECK(child);
                CHECK(parent.x >= x*(1<<level)); CHECK(parent.x < (x+1)*(1<<level));
                CHECK(parent.y >= z*(1<<level)); CHECK(parent.y < (z+1)*(1<<level));
            }
        }
    }
}

TEST_CASE("grass distance nodes match overlapping bare outlines, including duplicate triangles") {
    const auto ground=flat_ground();
    render_static_mesh bare;
    const std::vector<material_zone> zones{
        make_material_zone(material_zone_type::fairway,{{-2,-1},{2,-1},{2,1},{-2,1}}),
        make_material_zone(material_zone_type::bunker,{{1,-0.5f},{5,-0.5f},{5,1.5f},{1,1.5f}})};
    for(const auto& zone:zones) {
        const auto first=static_cast<std::uint32_t>(bare.vertices.size());
        for(const auto& p:zone.outline) {render_terrain_vertex v;v.position={p.x,3,p.y};bare.vertices.push_back(v);}
        for(const auto i:{0u,1u,2u,0u,2u,3u,0u,1u,2u}) {bare.indices.push_back(first+i);}
    }
    const auto field=bake_grass_field(ground,bare,{});
    for(float z=-10;z<=10;z+=0.5f) {
        for(float x=-10;x<=10;x+=0.5f) {
            const glm::vec3 p(x,0,z);
            const float exact=std::min({zone_distance_outside(zones[0],p),zone_distance_outside(zones[1],p),
                                        12-std::abs(x),12-std::abs(z)});
            // Rounded down to the clearance's step, never up.
            const float stored = sample_grass_field(field,{x,z}).y;
            CHECK(stored <= exact + 0.0001f);
            CHECK(exact - stored <= grass_field_distance_cap / 65535.0f + 0.0001f);
        }
    }
}
