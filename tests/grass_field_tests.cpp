#include "doctest.h"

#include "renderer/grass_field.h"
#include "test_support.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

TEST_CASE("the grass map covers the blades round the eye and is mapped again only once the eye nears its edge") {
    const float reach = 20.0f;
    const glm::vec3 eye(103.3f, 12.6f, -47.9f);
    const grass_window window = grass_window_around(eye, reach);
    CHECK(window.min.x < eye.x - reach);
    CHECK(window.min.y < eye.z - reach);
    CHECK(window.min.x + window.size > eye.x + reach);
    CHECK(window.min.y + window.size > eye.z + reach);
    // Its corner is on a whole texel, so the map does not crawl.
    const float texel = window.size / static_cast<float>(grass_map_texels);
    CHECK(std::abs(window.min.x / texel - std::round(window.min.x / texel)) < 1.0e-3f);
    CHECK(grass_window_serves(window, eye, reach));
    CHECK(grass_window_serves(window, eye + glm::vec3(3.0f, 0.0f, -3.0f), reach));
    CHECK(!grass_window_serves(window, eye + glm::vec3(reach, 0.0f, 0.0f), reach));
    CHECK(!grass_window_serves(grass_window{}, eye, reach));
}

TEST_CASE("the grass map looks straight down: x and z across it, higher ground nearer") {
    const grass_window window = grass_window_around(glm::vec3(0.0f, 5.0f, 0.0f), 20.0f);
    const glm::mat4 view_proj = grass_window_view_proj(window);
    const auto at = [&](const glm::vec3& point) { return glm::vec3(view_proj * glm::vec4(point, 1.0f)); };
    const glm::vec2 corner = window.min;
    const glm::vec2 far_corner = window.min + glm::vec2(window.size);
    CHECK(near(at(glm::vec3(corner.x, 5.0f, corner.y)).x, -1.0f));
    CHECK(near(at(glm::vec3(corner.x, 5.0f, corner.y)).y, -1.0f));
    CHECK(near(at(glm::vec3(far_corner.x, 5.0f, far_corner.y)).x, 1.0f));
    CHECK(near(at(glm::vec3(far_corner.x, 5.0f, far_corner.y)).y, 1.0f));
    CHECK(at(glm::vec3(0.0f, 9.0f, 0.0f)).z < at(glm::vec3(0.0f, 1.0f, 0.0f)).z);
    CHECK(std::abs(at(glm::vec3(0.0f, 60.0f, 0.0f)).z) < 1.0f);
}

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

TEST_CASE("only the blocks of tufts the eye may see over ground are drawn") {
    // Flat ground from x -100 to 100 and z -100 to 40; the eye looks along +z.
    render_static_mesh ground;
    ground.bounds = render_mesh_bounds{glm::vec3(-100.0f, 0.0f, -100.0f), glm::vec3(100.0f, 0.0f, 40.0f), true};
    const glm::vec3 eye(0.0f, 1.7f, 0.0f);
    const view_frustum frustum = make_view_frustum(glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f) *
                                                   glm::lookAt(eye, eye + glm::vec3(0.0f, -0.1f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    const grass_grid grid = grass_grid_around(eye, 60.0f, 0.5f);
    const float side = grid.spacing * static_cast<float>(grass_block_tufts);
    const int blocks = grid.per_row / grass_block_tufts;
    const std::vector<glm::vec2> seen = grass_visible_blocks(grid, frustum, {&ground}, 0.3f, eye, 0.0f);
    CHECK(!seen.empty());
    CHECK(static_cast<int>(seen.size()) < blocks * blocks / 2);
    bool ahead = false;
    for (const glm::vec2& corner : seen) {
        // Not behind the eye, and not past the ground's end.
        CHECK(corner.y + side + grid.spacing > eye.z);
        CHECK(corner.y <= 40.0f);
        ahead = ahead || (corner.x <= 0.0f && corner.x + side > 0.0f && corner.y > 10.0f);
    }
    CHECK(ahead);
    // An outer ring leaves out the blocks wholly inside the ring within.
    const std::vector<glm::vec2> hollow = grass_visible_blocks(grid, frustum, {&ground}, 0.3f, eye, 20.0f);
    CHECK(hollow.size() < seen.size());
    for (const glm::vec2& corner : hollow) {
        const glm::vec2 far_corner = corner + glm::vec2(side + grid.spacing);
        CHECK(glm::length(glm::max(glm::abs(corner), glm::abs(far_corner))) >= 20.0f);
    }
    CHECK(grass_visible_blocks(grid, frustum, {}, 0.3f, eye, 0.0f).empty());
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

TEST_CASE("a tee box's top is bare on the grass map, a little past its edges and nowhere else") {
    tee_box box;
    box.center = glm::vec3(30.0f, 4.0f, -12.0f);
    box.down_hole = glm::vec3(0.0f, 0.0f, 1.0f);
    box.half_width = 1.5f;
    box.half_length = 2.5f;
    const std::vector<render_terrain_vertex> top = grass_bare_tee_tops({box});
    REQUIRE(top.size() == 6U);
    for (const render_terrain_vertex& v : top) {
        CHECK(v.blades == 0.0f);
        CHECK(v.position.y == 4.0f);
        // Across the hole is x here, down it z.
        CHECK(std::abs(v.position.x - 30.0f) > 1.5f);
        CHECK(std::abs(v.position.x - 30.0f) < 1.8f);
        CHECK(std::abs(v.position.z + 12.0f) > 2.5f);
        CHECK(std::abs(v.position.z + 12.0f) < 2.8f);
    }
}
