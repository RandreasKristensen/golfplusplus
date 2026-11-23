#include "doctest.h"

#include "game/lighting_fields.h"
#include "renderer/caster_index.h"
#include "renderer/fence_batch.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/prop_batch.h"
#include "renderer/prop_glow.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/world_marker_batch.h"
#include "renderer/shadow_casters.h"
#include "renderer/shadow_map_fit.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace {
float horizontal_gap(const glm::vec3& a, const glm::vec3& b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

bool inside_runs(const std::vector<caster_run>& runs, const std::size_t vertex) {
    return std::any_of(runs.begin(), runs.end(),
                       [vertex](const caster_run& run) { return vertex >= run.first && vertex < run.first + run.count; });
}
}

TEST_CASE("static casters: every triangle with a corner in reach is in a selected run, negative coordinates and long ones included") {
    std::mt19937 random(7);
    std::uniform_real_distribution<float> place(-400.0f, 400.0f);
    std::uniform_real_distribution<float> span(0.1f, 90.0f);
    std::vector<glm::vec3> corners;
    for (int t = 0; t < 600; ++t) {
        const glm::vec3 at(place(random), 0.0f, place(random));
        corners.push_back(at);
        corners.push_back(at + glm::vec3(span(random), 2.0f, 0.0f));
        corners.push_back(at + glm::vec3(0.0f, 1.0f, span(random)));
    }
    caster_index index;
    const std::vector<glm::vec3> sorted = index.build(corners);
    REQUIRE(sorted.size() == corners.size());
    // A permutation of the same corners, in whole triangles.
    for (const glm::vec3& corner : corners) {
        CHECK(std::find(sorted.begin(), sorted.end(), corner) != sorted.end());
    }
    for (const glm::vec3 eye : {glm::vec3(0.0f), glm::vec3(-250.0f, 3.0f, 120.0f), glm::vec3(399.0f, 1.0f, -399.0f)}) {
        for (const float reach : {5.0f, 60.0f, 160.0f}) {
            const std::vector<caster_run> runs = index.select(eye, reach);
            std::size_t selected = 0;
            for (const caster_run& run : runs) { selected += run.count; }
            CHECK(selected < sorted.size() + 1);
            for (std::size_t i = 0; i + 2 < sorted.size(); i += 3) {
                const bool near = horizontal_gap(sorted[i], eye) <= reach || horizontal_gap(sorted[i + 1], eye) <= reach ||
                                  horizontal_gap(sorted[i + 2], eye) <= reach;
                if (near) {
                    CHECK(inside_runs(runs, i));
                    CHECK(inside_runs(runs, i + 2));
                }
            }
        }
    }
    // A short reach selects less than the whole list.
    std::size_t few = 0;
    for (const caster_run& run : index.select(glm::vec3(0.0f), 30.0f)) { few += run.count; }
    CHECK(few < sorted.size());
}

TEST_CASE("hole signs, way signs and fence posts cast where the map reaches, from one static list") {
    render_hole_signs signs;
    render_fences fences;
    const auto triangle_at = [](const float x) {
        return std::vector<glm::vec3>{glm::vec3(x, 0.0f, 0.0f), glm::vec3(x + 1.0f, 0.0f, 0.0f), glm::vec3(x, 2.0f, 0.0f)};
    };
    for (const float x : {10.0f, 1000.0f}) {
        for (const glm::vec3& corner : triangle_at(x)) {
            hole_sign_vertex sign;
            sign.position = corner;
            signs.vertices.push_back(sign);
            fence_vertex post;
            post.position = corner + glm::vec3(0.0f, 0.0f, 5.0f);
            fences.posts.push_back(post);
        }
    }
    const std::vector<glm::vec3> corners = static_caster_corners(&signs, &fences);
    REQUIRE(corners.size() == 12U);
    CHECK(static_caster_corners(nullptr, nullptr).empty());
    caster_index index;
    const std::vector<glm::vec3> sorted = index.build(corners);
    std::size_t selected = 0;
    for (const caster_run& run : index.select(glm::vec3(0.0f, 2.0f, 0.0f), 160.0f)) {
        for (std::size_t i = run.first; i < run.first + run.count; ++i) {
            CHECK(sorted[i].x < 20.0f);
            ++selected;
        }
    }
    // The near sign's and post's triangles; not the far ones.
    CHECK(selected == 6U);
}

TEST_CASE("a static model's glow floors are slots the frame fills: the same model, never a baked night glow") {
    const glow_floors slots = glow_floor_slots();
    glow_floors night;
    float value = 0.1f;
    for (const share_field<glow_floors>& field : glow_fields) {
        night.*field.member = value;
        value += 0.1f;
    }
    const std::array<glm::vec4, 2> table = glow_floor_table(night);
    std::size_t slot = 0;
    for (const share_field<glow_floors>& field : glow_fields) {
        CHECK(slots.*field.member == -static_cast<float>(slot + 1));
        CHECK(table[slot / 4][static_cast<int>(slot % 4)] == night.*field.member);
        ++slot;
    }
    // Every shipped model: slot encoding changes only the glow, which resolves to what the real floors give.
    for (const auto& [id, model] : test_props()) {
        const glm::mat4 frame(1.0f);
        prop_batch cached, direct;
        append_prop_model(cached, model, frame, slots);
        append_prop_model(direct, model, frame, night);
        const std::array<std::vector<prop_vertex>, 2> cached_lists{cached.opaque_vertices(), cached.blended_vertices()};
        const std::array<std::vector<prop_vertex>, 2> direct_lists{direct.opaque_vertices(), direct.blended_vertices()};
        REQUIRE(cached_lists[0].size() == direct_lists[0].size());
        REQUIRE(cached_lists[1].size() == direct_lists[1].size());
        const auto resolved = [&table](const float glow) {
            if (glow >= 0.0f) { return glow; }
            const int index = static_cast<int>(-glow - 1.0f);
            return table[static_cast<std::size_t>(index / 4)][index % 4];
        };
        for (std::size_t list = 0; list < 2; ++list) {
            const std::vector<prop_vertex>& a = cached_lists[list];
            const std::vector<prop_vertex>& b = direct_lists[list];
            for (std::size_t i = 0; i < a.size(); ++i) {
                CHECK(a[i].position == b[i].position);
                CHECK(a[i].normal == b[i].normal);
                CHECK(a[i].color == b[i].color);
                CHECK(a[i].alpha == b[i].alpha);
                CHECK(std::abs(resolved(a[i].glow) - b[i].glow) < 1e-6f);
            }
        }
    }
}

namespace {
// Every vertex a draw of `view` puts on screen: the batch's triangles and the figures' placed meshes.
std::size_t vertices_in(const world_marker_batch& batch, const marker_view view) {
    std::size_t total = 0;
    for (const world_marker_run& run : batch.runs()) {
        if (batch.shown_in(run, view)) { total += run.count; }
    }
    const canonical_meshes meshes;
    for (const world_marker_instance_run& run : batch.instance_runs()) {
        if (batch.shown_in(run.part, view)) { total += run.count * meshes.vertex_count(run.mesh); }
    }
    return total;
}

std::size_t drawn_vertices(const world_marker_batch& batch) {
    return batch.vertices().size() + batch.instanced_vertices().size();
}
}

TEST_CASE("one marker upload serves each view: shoes in the main one, my figure in mirrors and shadows, only casters in shadows") {
    const scene_colors& colors = test_scene_colors();
    const std::vector<glm::vec3> pins = {glm::vec3(1.0f, 0.0f, 1.0f)};
    render_address_feet feet;
    render_remote_avatar other, mine;
    other.position = glm::vec3(4.0f, 0.0f, 0.0f);
    mine.position = glm::vec3(-4.0f, 0.0f, 0.0f);
    const std::vector<render_remote_avatar> others = {other};
    const std::vector<render_remote_ball> balls = {render_remote_ball{glm::vec3(2.0f, 0.1f, 2.0f), 3}};
    world_marker_scene scene;
    scene.show_hole = true;
    scene.pin_position = glm::vec3(8.0f, 0.0f, 8.0f);
    scene.pin_markers = &pins;
    scene.cup_radius_meters = 0.054f;
    scene.cup_depth_meters = 0.1f;
    scene.pin_visual_height_meters = 2.0f;
    scene.flagstick_radius_meters = 0.01f;
    scene.ball_visual_radius_meters = 0.02f;
    scene.avatar_eye_height = 1.6f;
    scene.remote_avatars = &others;
    scene.remote_balls = &balls;

    const auto built = [&](const bool with_feet, const bool with_mine) {
        world_marker_scene variant = scene;
        variant.address_feet = with_feet ? &feet : nullptr;
        variant.my_figure = with_mine ? &mine : nullptr;
        world_marker_batch batch;
        build_world_marker_batch(batch, variant, colors);
        return batch;
    };
    const world_marker_batch bare = built(false, false);
    const world_marker_batch all = built(true, true);
    const std::size_t feet_vertices = drawn_vertices(built(true, false)) - drawn_vertices(bare);
    const std::size_t mine_vertices = drawn_vertices(built(false, true)) - drawn_vertices(bare);
    REQUIRE(feet_vertices > 0);
    REQUIRE(mine_vertices > 0);
    CHECK(drawn_vertices(all) == drawn_vertices(bare) + feet_vertices + mine_vertices);
    CHECK(vertices_in(all, marker_view::scene) == drawn_vertices(bare) + feet_vertices);
    CHECK(vertices_in(all, marker_view::mirror) == drawn_vertices(bare) + mine_vertices);
    // Shadows: no cups, balls, collectibles or shoes; flagsticks and figures only.
    world_marker_batch casters_only;
    world_marker_scene flags = scene;
    flags.remote_balls = nullptr;
    flags.my_figure = &mine;
    build_world_marker_batch(casters_only, flags, colors);
    std::size_t expected = 0;
    for (const world_marker_run& run : casters_only.runs()) {
        if (run.stencil == marker_stencil::none) { expected += run.count; }
    }
    // The figures, mine and the other's, all cast; the balls and the shoes are not in this batch.
    expected += casters_only.instanced_vertices().size();
    CHECK(vertices_in(all, marker_view::sun_shadow) == expected);
    CHECK(vertices_in(all, marker_view::sun_shadow) < vertices_in(all, marker_view::mirror));
    // A batch that is not built in parts (the tee boxes) shows every run in every view.
    world_marker_batch plain;
    plain.append_disc(glm::mat4(1.0f), glm::vec3(1.0f), 0.0f, true);
    for (const marker_view view : {marker_view::scene, marker_view::mirror, marker_view::sun_shadow}) {
        CHECK(vertices_in(plain, view) == plain.vertices().size());
    }
}

TEST_CASE("static casters selected for the sun's map project into it; ones the selection leaves out fall outside it") {
    const float extent = 160.0f;
    const glm::vec3 eye(30.0f, 3.0f, -20.0f);
    for (const glm::vec3 sun : {glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)), glm::normalize(glm::vec3(-0.9f, 0.3f, -0.2f))}) {
        const shadow_map_fit fit = fit_shadow_map(sun, eye, extent, 256);
        std::vector<glm::vec3> corners;
        // A ring of posts at several distances from the eye, round all headings.
        for (const float distance : {20.0f, 90.0f, 150.0f, 400.0f, 900.0f}) {
            for (int heading = 0; heading < 12; ++heading) {
                const float angle = static_cast<float>(heading) * 0.5236f;
                const glm::vec3 at = eye + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * distance - glm::vec3(0.0f, 3.0f, 0.0f);
                corners.insert(corners.end(), {at, at + glm::vec3(0.1f, 0.0f, 0.0f), at + glm::vec3(0.0f, 1.8f, 0.0f)});
            }
        }
        caster_index index;
        const std::vector<glm::vec3> sorted = index.build(corners);
        const std::vector<caster_run> runs = index.select(eye, extent);
        for (std::size_t i = 0; i + 2 < sorted.size(); i += 3) {
            const glm::vec4 clip = fit.view_proj * glm::vec4(sorted[i], 1.0f);
            const bool inside_map = std::abs(clip.x) <= 1.0f && std::abs(clip.y) <= 1.0f;
            const bool selected = inside_runs(runs, i);
            // Every post on the map is drawn, and nothing far past it is.
            if (inside_map) { CHECK(selected); }
            if (horizontal_gap(sorted[i], eye) > 400.0f) { CHECK(!inside_map); }
            if (horizontal_gap(sorted[i], eye) > extent + 3.0f * caster_index::tile_meters) { CHECK(!selected); }
        }
    }
}
