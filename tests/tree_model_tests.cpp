#include "doctest.h"

#include "game/lighting_loader.h"
#include "renderer/frustum.h"
#include "renderer/render_tree.h"
#include "renderer/tree_model.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
const lighting_palette& shipped_palette() {
    static const lighting_palette palette = *load_lighting(asset_root());
    return palette;
}

const tree_look& look_named(const char* name) {
    const std::vector<tree_look>& looks = shipped_palette().trees;
    return *std::find_if(looks.begin(), looks.end(), [&](const tree_look& look) { return look.name == name; });
}

tree_body standing(const glm::vec3& base, const std::uint8_t species = 0) {
    tree_body tree;
    tree.base = base;
    tree.shape = tree_shape{0.4f, 5.0f, 4.0f, 9.0f};
    tree.shape.species = species;
    return tree;
}
}

TEST_CASE("every species the trees are of has a look to be drawn in") {
    for (const tree_species& species : shipped_content().tuning.trees.species) {
        const std::vector<tree_look>& looks = shipped_palette().trees;
        CHECK(std::any_of(looks.begin(), looks.end(), [&](const tree_look& look) { return look.name == species.name; }));
    }
}

TEST_CASE("a grown tree is the same for its seed, its bark first, and stands in its unit frame") {
    for (const tree_look& look : shipped_palette().trees) {
        const grown_tree tree = grow_tree(look, 7U);
        const grown_tree again = grow_tree(look, 7U);
        const grown_tree other = grow_tree(look, 8U);
        REQUIRE(!tree.vertices.empty());
        REQUIRE(tree.vertices.size() % 3U == 0U);
        CHECK(tree.vertices.size() == again.vertices.size());
        CHECK(tree.vertices[tree.vertices.size() / 2].position == again.vertices[again.vertices.size() / 2].position);
        CHECK(tree.vertices.back().position != other.vertices.back().position);
        CHECK(tree.bark_count > 0U);
        CHECK(tree.bark_count < tree.vertices.size());
        for (std::size_t i = 0; i < tree.vertices.size(); ++i) {
            const grown_tree_vertex& v = tree.vertices[i];
            CHECK(v.leaf == (i < tree.bark_count ? 0.0f : 1.0f));
            CHECK(v.position.y >= -0.05f);
            CHECK(v.position.y <= 1.25f);
            CHECK(std::abs(v.position.x) <= 1.4f);
            CHECK(std::abs(v.position.z) <= 1.4f);
            CHECK(near(glm::length(v.normal), 1.0f, 0.01f));
        }
    }
}

TEST_CASE("a crown's shape is 0 at its middle and 1 at its surface; a spruce narrows to its top") {
    const tree_look& beech = look_named("beech");
    const float middle_y = beech.trunk_share + 0.5f * (1.0f - beech.trunk_share);
    CHECK(near(crown_reach(beech, glm::vec3(0.0f, middle_y, 0.0f)), 0.0f));
    CHECK(near(crown_reach(beech, glm::vec3(1.0f, middle_y, 0.0f)), 1.0f));
    CHECK(crown_reach(beech, glm::vec3(0.0f, 0.05f, 0.0f)) > 1.0f);
    const tree_look& spruce = look_named("spruce");
    CHECK(crown_reach(spruce, glm::vec3(0.5f, 0.3f, 0.0f)) < 1.0f);
    CHECK(crown_reach(spruce, glm::vec3(0.5f, 0.9f, 0.0f)) > 1.0f);
    // A bush is all crown from the ground.
    CHECK(crown_reach(look_named("scrub"), glm::vec3(0.5f, 0.2f, 0.0f)) < 1.0f);
}

TEST_CASE("a tree stands at its foot, as tall as it is and as wide as its crown, turned about its trunk") {
    const tree_body tree = standing(glm::vec3(10.0f, 2.0f, -4.0f));
    const render_tree_instance instance = tree_instance_for(tree);
    CHECK(near(tree_world_point(instance, glm::vec3(0.0f)), tree.base));
    CHECK(near(tree_world_point(instance, glm::vec3(0.0f, 1.0f, 0.0f)), tree.base + glm::vec3(0.0f, 14.0f, 0.0f)));
    const glm::vec3 edge = tree_world_point(instance, glm::vec3(1.0f, 0.5f, 0.0f)) - tree.base;
    CHECK(near(glm::length(glm::vec2(edge.x, edge.z)), 4.0f));
    CHECK(near(edge.y, 7.0f));
    CHECK(instance.tint >= 0.0f);
    CHECK(instance.tint <= 1.0f);
    CHECK(tree_variant_for(tree) >= 0);
    CHECK(tree_variant_for(tree) < tree_variants);
}

TEST_CASE("the trees in view are bucketed by species and variant; those behind are left out") {
    const glm::vec3 eye(0.0f, 2.0f, 0.0f);
    const glm::mat4 view_proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 2000.0f) *
        glm::lookAt(eye, glm::vec3(0.0f, 2.0f, 100.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const view_frustum frustum = make_view_frustum(view_proj);
    const std::vector<tree_body> trees{standing(glm::vec3(0.0f, 0.0f, 20.0f), 0), standing(glm::vec3(3.0f, 0.0f, 300.0f), 0),
                                       standing(glm::vec3(-2.0f, 0.0f, 25.0f), 1), standing(glm::vec3(0.0f, 0.0f, -50.0f), 0)};
    const render_tree_batch batch = build_tree_batch(trees, &frustum);
    CHECK(batch.instances.size() == 3U);
    std::size_t counted = 0;
    for (const render_tree_bucket& bucket : batch.buckets) {
        CHECK(bucket.count > 0U);
        CHECK(bucket.first == counted);
        counted += bucket.count;
    }
    CHECK(counted == batch.instances.size());
    // The shadow maps take the same models, without eye-dependent selection.
    const render_tree_batch casters = build_tree_batch(trees, nullptr);
    CHECK(casters.instances.size() == trees.size());
}
