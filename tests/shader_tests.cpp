#include "doctest.h"

#include "renderer/render_mesh.h"
#include "renderer/render_tree.h"
#include "renderer/shader.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace {
// Stands in for glGetUniformLocation: counts calls and optimises out any name
// starting with "u_dead" (returns -1) like a real linker would.
struct fake_uniform_query {
    int* calls = nullptr;

    int operator()(const char* name) const {
        ++*calls;
        if (std::strncmp(name, "u_dead", 6) == 0) {
            return -1;
        }
        return static_cast<int>(std::strlen(name));
    }
};
}

TEST_CASE("uniform location cache queries each name once") {
    uniform_location_cache cache;
    int calls = 0;
    const fake_uniform_query query{&calls};

    for (int frame = 0; frame < 100; ++frame) {
        CHECK(cache.find_or_query("u_model", query) == 7);
        CHECK(cache.find_or_query("u_mvp", query) == 5);
        CHECK(cache.find_or_query("u_color", query) == 7);
        CHECK(cache.find_or_query("u_use_vertex_color", query) == 18);
    }

    CHECK(calls == 4);
    CHECK(cache.size() == 4U);
}

TEST_CASE("uniform location cache also caches optimised-out uniforms") {
    uniform_location_cache cache;
    int calls = 0;
    const fake_uniform_query query{&calls};

    CHECK(cache.find_or_query("u_dead_alpha", query) == -1);
    CHECK(cache.find_or_query("u_dead_alpha", query) == -1);
    CHECK(calls == 1);
}

TEST_CASE("uniform location cache keys by content, not pointer") {
    uniform_location_cache cache;
    int calls = 0;
    const fake_uniform_query query{&calls};

    // Same text at a different address hits the cache.
    std::string copy = "u_model";
    CHECK(cache.find_or_query("u_model", query) == 7);
    CHECK(cache.find_or_query(copy.c_str(), query) == 7);
    CHECK(calls == 1);

    // Same address with different text must not return the stale entry.
    char buffer[16] = "u_mvp";
    CHECK(cache.find_or_query(buffer, query) == 5);
    std::memcpy(buffer, "u_dead_mvp", sizeof("u_dead_mvp"));
    CHECK(cache.find_or_query(buffer, query) == -1);
    CHECK(calls == 3);
}

TEST_CASE("uniform location cache clear forces a requery") {
    uniform_location_cache cache;
    int calls = 0;
    const fake_uniform_query query{&calls};

    cache.find_or_query("u_color", query);
    cache.clear();
    CHECK(cache.size() == 0U);
    cache.find_or_query("u_color", query);
    CHECK(calls == 2);
}

TEST_CASE("uniform location cache stays correct when full or given long names") {
    uniform_location_cache cache;
    int calls = 0;
    const fake_uniform_query query{&calls};

    for (std::size_t i = 0; i < uniform_location_cache::capacity; ++i) {
        const std::string name = "u_slot_" + std::to_string(i);
        cache.find_or_query(name.c_str(), query);
    }
    CHECK(cache.size() == uniform_location_cache::capacity);
    CHECK(calls == static_cast<int>(uniform_location_cache::capacity));

    // Overflow names are answered correctly, just not stored.
    CHECK(cache.find_or_query("u_overflow", query) == 10);
    CHECK(cache.find_or_query("u_overflow", query) == 10);
    CHECK(calls == static_cast<int>(uniform_location_cache::capacity) + 2);

    uniform_location_cache fresh;
    const std::string long_name(uniform_location_cache::max_name_length + 1, 'x');
    CHECK(fresh.find_or_query(long_name.c_str(), query) == static_cast<int>(long_name.size()));
    CHECK(fresh.size() == 0U);

    const std::string longest_name(uniform_location_cache::max_name_length, 'y');
    CHECK(fresh.find_or_query(longest_name.c_str(), query) == static_cast<int>(longest_name.size()));
    CHECK(fresh.size() == 1U);
}

namespace {
render_terrain_vertex vertex_at(const glm::vec3& position) {
    render_terrain_vertex vertex;
    vertex.position = position;
    return vertex;
}
}

TEST_CASE("render mesh bounds are invalid for an empty mesh") {
    const render_mesh_bounds bounds = compute_render_mesh_bounds({});
    CHECK(!bounds.valid);
    CHECK(bounds.min == glm::vec3(0.0f));
    CHECK(bounds.max == glm::vec3(0.0f));

}

TEST_CASE("render mesh bounds of a single vertex collapse to that vertex") {
    const glm::vec3 point(3.0f, -1.5f, 7.25f);
    const render_mesh_bounds bounds = compute_render_mesh_bounds({vertex_at(point)});
    CHECK(bounds.valid);
    CHECK(bounds.min == point);
    CHECK(bounds.max == point);
}

TEST_CASE("render mesh bounds cover every axis independently") {
    const std::vector<render_terrain_vertex> vertices = {
        vertex_at(glm::vec3(4.0f, 2.0f, -3.0f)),
        vertex_at(glm::vec3(-6.0f, -0.5f, 9.0f)),
        vertex_at(glm::vec3(1.0f, 5.0f, 0.0f)),
    };
    const render_mesh_bounds bounds = compute_render_mesh_bounds(vertices);
    CHECK(bounds.valid);
    CHECK(bounds.min == glm::vec3(-6.0f, -0.5f, -3.0f));
    CHECK(bounds.max == glm::vec3(4.0f, 5.0f, 9.0f));

}

namespace {
// A tree's trunk and leaves as model matrices on the unit cylinder and cone.
glm::mat4 trunk_model(const tree_body& tree) {
    const tree_shape& shape = tree.shape;
    return glm::scale(glm::translate(glm::mat4(1.0f), tree.base),
                      glm::vec3(shape.trunk_radius, shape.trunk_height, shape.trunk_radius));
}

glm::mat4 leaf_model(const tree_body& tree) {
    const tree_shape& shape = tree.shape;
    return glm::scale(glm::translate(glm::mat4(1.0f), tree.base + glm::vec3(0.0f, shape.trunk_height, 0.0f)),
                      glm::vec3(shape.leaf_radius, shape.leaf_height, shape.leaf_radius));
}

// What tree_instanced.vert computes for a local vertex.
glm::vec3 instance_world_position(const render_tree_instance& instance, const glm::vec3& local) {
    return instance.offset + local * instance.scale;
}

void check_instance_matches_model(const render_tree_instance& instance, const glm::mat4& model) {
    const glm::mat4 instance_model = glm::scale(glm::translate(glm::mat4(1.0f), instance.offset), instance.scale);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(instance_model[column][row] == model[column][row]);
        }
    }

    const std::vector<glm::vec3> local_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(-0.70710677f, 1.0f, 0.70710677f),
    };
    for (const glm::vec3& local : local_points) {
        const glm::vec4 expected = model * glm::vec4(local, 1.0f);
        const glm::vec3 actual = instance_world_position(instance, local);
        CHECK(std::abs(actual.x - expected.x) <= 1e-5f);
        CHECK(std::abs(actual.y - expected.y) <= 1e-5f);
        CHECK(std::abs(actual.z - expected.z) <= 1e-5f);
    }
}
}

TEST_CASE("tree instances match the trunk and leaf model matrices") {
    const std::vector<tree_body> trees{
        tree_body{glm::vec3(12.5f, 3.25f, -40.0f), tree_shape{0.35f, 2.4f, 1.6f, 3.2f}},
        tree_body{glm::vec3(-7.0f, 0.5f, 18.0f), tree_shape{0.2f, 1.75f, 2.4f, 4.5f}},
        tree_body{glm::vec3(0.0f), tree_shape{0.01f, 0.01f, 0.01f, 0.01f}},
    };

    const render_tree_instance_batch batch = build_tree_instances(trees);
    REQUIRE(batch.trunks.size() == trees.size());
    REQUIRE(batch.leaves.size() == trees.size());
    for (std::size_t i = 0; i < trees.size(); ++i) {
        check_instance_matches_model(batch.trunks[i], trunk_model(trees[i]));
        check_instance_matches_model(batch.leaves[i], leaf_model(trees[i]));
    }
}

TEST_CASE("tree instances are empty for no trees") {
    const render_tree_instance_batch batch = build_tree_instances({});
    CHECK(batch.trunks.empty());
    CHECK(batch.leaves.empty());
}

TEST_CASE("tree instance packs into six floats for the instance buffer") {
    CHECK(sizeof(render_tree_instance) == 6 * sizeof(float));
    CHECK(offsetof(render_tree_instance, scale) == 3 * sizeof(float));
}
