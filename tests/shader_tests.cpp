#include "doctest.h"

#include "renderer/lighting_uniforms.h"
#include "renderer/primitive_mesh.h"
#include "renderer/render_mesh.h"
#include "renderer/render_tree.h"
#include "renderer/scene_haze.h"
#include "renderer/scene_lights.h"
#include "renderer/shader.h"
#include "renderer/shader_source.h"
#include "test_support.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
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

TEST_CASE("tree cones and trunks carry outward normals for their light") {
    for (const std::vector<float>& mesh : {make_cone_vertices(primitive_cone_segments), make_cylinder_vertices(primitive_cylinder_segments)}) {
        REQUIRE(mesh.size() % 6U == 0U);
        for (std::size_t i = 0; i < mesh.size(); i += 6U) {
            const glm::vec3 position(mesh[i], mesh[i + 1], mesh[i + 2]);
            const glm::vec3 normal(mesh[i + 3], mesh[i + 4], mesh[i + 5]);
            CHECK(near(glm::length(normal), 1.0f, 0.0001f));
            // Sides face away from the axis, caps straight up or down.
            const glm::vec3 out = std::abs(normal.y) > 0.999f ? glm::vec3(0.0f, normal.y, 0.0f) : glm::vec3(position.x, 0.0f, position.z);
            CHECK(glm::dot(normal, out) >= 0.0f);
        }
    }
}

TEST_CASE("shader includes pull in a file's text in place of their line") {
    const shader_include_reader read = [](const std::string& name) -> std::optional<std::string> {
        if (name == "light.glsl") {
            return std::string("float light() { return 1.0; }\n");
        }
        if (name == "nested.glsl") {
            return std::string("#include \"light.glsl\"\n");
        }
        return std::nullopt;
    };
    const std::optional<std::string> expanded =
        expand_shader_includes("#version 330 core\n  #include \"light.glsl\"  \nvoid main() {}\n", read);
    REQUIRE(expanded.has_value());
    CHECK(*expanded == "#version 330 core\nfloat light() { return 1.0; }\nvoid main() {}\n");
    CHECK(*expand_shader_includes("no includes", read) == "no includes");
    CHECK(!expand_shader_includes("#include \"missing.glsl\"\n", read));
    CHECK(!expand_shader_includes("#include \"nested.glsl\"\n", read));
}

TEST_CASE("lit scene shaders take their light from lighting.glsl, none from a uniform of their own") {
    const std::filesystem::path shaders = std::filesystem::path(asset_root()) / "picture" / "shaders";
    const shader_include_reader read = [&shaders](const std::string& name) { return read_text_file(shaders / name); };
    for (const char* name : {"terrain.frag", "ball.frag", "hole_sign.frag", "fence.frag", "water.frag", "underwater.frag",
                             "tree_instanced.vert", "world_marker.vert", "tee_box.vert", "backdrop.frag", "blob_shadow.vert",
                             "floodlight.vert", "weather_streak.vert"}) {
        const std::optional<std::string> source = read_text_file(shaders / name);
        REQUIRE(source.has_value());
        const std::optional<std::string> expanded = expand_shader_includes(*source, read);
        REQUIRE(expanded.has_value());
        CHECK(expanded->find(frame_lighting_block_name) != std::string::npos);
    }
    for (const auto& entry : std::filesystem::directory_iterator(shaders)) {
        const std::optional<std::string> source = read_text_file(entry.path());
        REQUIRE(source.has_value());
        CHECK(source->find("u_light_dir") == std::string::npos);
        CHECK(source->find("u_haze_color") == std::string::npos);
    }
}

TEST_CASE("the frame's light and haze pack into the std140 block as lighting.glsl reads it") {
    render_lighting lighting;
    lighting.sun_direction = glm::vec3(0.0f, 0.6f, 0.8f);
    lighting.sun_color = glm::vec3(1.0f, 0.5f, 0.25f);
    lighting.ambient_sky = glm::vec3(0.2f, 0.3f, 0.4f);
    lighting.ambient_ground = glm::vec3(0.1f, 0.15f, 0.05f);
    lighting.sky_zenith = glm::vec3(0.3f, 0.5f, 0.8f);
    lighting.sky_blend = 0.25f;
    lighting.horizon_glow = glm::vec3(0.9f, 0.4f, 0.2f);
    lighting.horizon_glow_strength = 0.75f;
    lighting.moon_direction = glm::vec3(0.0f, 0.8f, -0.6f);
    lighting.moon_phase = 0.4f;
    lighting.star_visibility = 0.5f;
    lighting.shadows = render_shadows{0.75f, 0.25f, 4.0f, 0.05f};
    lighting.lamp_glow = glm::vec3(2.5f, 2.25f, 2.0f);
    scene_haze haze;
    haze.color = glm::vec3(0.7f, 0.8f, 0.9f);
    haze.far_amount = 0.6f;
    haze.full_distance = 400.0f;
    haze.eye = glm::vec3(1.0f, 2.0f, 3.0f);
    render_light spot;
    spot.position = glm::vec3(1.0f, 2.0f, 3.0f);
    spot.color = glm::vec3(2.0f, 1.5f, 1.0f);
    spot.radius = 12.0f;
    spot.direction = glm::vec3(0.0f, 0.0f, 1.0f);
    spot.cone_edge_cos = 0.8f;
    spot.cone_full_cos = 0.9f;
    const frame_lighting_block block = make_frame_lighting_block(lighting, haze, {spot});
    lighting.weather.mottle = -0.25f;
    lighting.weather.rainbow = 0.5f;
    lighting.weather.cover = 0.75f;
    lighting.weather.haze_gain = 0.2f;
    lighting.weather.wet_albedo = 0.7f;
    lighting.weather.wet_gloss = 0.3f;
    lighting.weather.wet_gloss_power = 24.0f;
    const frame_lighting_block weathered = make_frame_lighting_block(lighting, haze, {});
    CHECK(weathered.weather == glm::vec4(-0.25f, 0.5f, 0.75f, 0.2f));
    CHECK(weathered.wet == glm::vec4(0.7f, 0.3f, 24.0f, 0.0f));
    CHECK(block.weather == glm::vec4(0.0f));
    CHECK(block.wet == glm::vec4(1.0f, 0.0f, 1.0f, 0.0f));
    CHECK(sizeof(frame_lighting_block) == 240U + 48U * max_scene_lights);
    CHECK(offsetof(frame_lighting_block, sun_color) == 16U);
    CHECK(offsetof(frame_lighting_block, sky_light) == 32U);
    CHECK(offsetof(frame_lighting_block, ground_light) == 48U);
    CHECK(offsetof(frame_lighting_block, sky_zenith) == 64U);
    CHECK(offsetof(frame_lighting_block, haze_color) == 80U);
    CHECK(offsetof(frame_lighting_block, haze_eye) == 96U);
    CHECK(offsetof(frame_lighting_block, moon_direction) == 112U);
    CHECK(offsetof(frame_lighting_block, horizon_glow) == 128U);
    CHECK(offsetof(frame_lighting_block, sun_horizon_a) == 144U);
    CHECK(offsetof(frame_lighting_block, sun_horizon_b) == 160U);
    CHECK(offsetof(frame_lighting_block, shadow) == 176U);
    CHECK(offsetof(frame_lighting_block, lamp_glow) == 192U);
    CHECK(offsetof(frame_lighting_block, weather) == 208U);
    CHECK(offsetof(frame_lighting_block, wet) == 224U);
    CHECK(offsetof(frame_lighting_block, light_position) == 240U);
    CHECK(offsetof(frame_lighting_block, light_color) == 240U + 16U * max_scene_lights);
    CHECK(offsetof(frame_lighting_block, light_cone) == 240U + 32U * max_scene_lights);
    CHECK(block.sun_direction == glm::vec4(0.0f, 0.6f, 0.8f, 0.5f));
    CHECK(block.sun_color == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));
    CHECK(block.sky_light == glm::vec4(0.2f, 0.3f, 0.4f, 0.0f));
    CHECK(block.ground_light == glm::vec4(0.1f, 0.15f, 0.05f, 0.0f));
    CHECK(block.sky_zenith == glm::vec4(0.3f, 0.5f, 0.8f, 0.25f));
    CHECK(block.haze_color == glm::vec4(0.7f, 0.8f, 0.9f, 0.6f));
    CHECK(block.haze_eye == glm::vec4(1.0f, 2.0f, 3.0f, 1.0f / 400.0f));
    CHECK(block.moon_direction == glm::vec4(0.0f, 0.8f, -0.6f, 0.4f));
    CHECK(block.horizon_glow == glm::vec4(0.9f, 0.4f, 0.2f, 0.75f));
    CHECK(block.shadow == glm::vec4(0.75f, 0.25f, 0.0f, 0.05f));
    CHECK(block.lamp_glow == glm::vec4(2.5f, 2.25f, 2.0f, 0.0f));
    // The sun lies along +z: horizon way 2.
    CHECK(block.sun_horizon_a == glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    CHECK(block.sun_horizon_b == glm::vec4(0.0f));
    CHECK(block.light_position[0] == glm::vec4(1.0f, 2.0f, 3.0f, 12.0f));
    CHECK(block.light_color[0] == glm::vec4(2.0f, 1.5f, 1.0f, 0.9f));
    CHECK(block.light_cone[0] == glm::vec4(0.0f, 0.0f, 1.0f, 0.8f));
    // The rest light nothing.
    for (std::size_t i = 1; i < max_scene_lights; ++i) {
        CHECK(block.light_position[i].w == 0.0f);
        CHECK(block.light_color[i] == glm::vec4(0.0f, 0.0f, 0.0f, -1.0f));
    }
}

TEST_CASE("the scene is lit by the lights nearest the camera, by how far each one's reach is from it") {
    const auto light_at = [](const float x, const float radius) {
        render_light light;
        light.position = glm::vec3(x, 0.0f, 0.0f);
        light.color = glm::vec3(1.0f);
        light.radius = radius;
        return light;
    };
    std::vector<render_light> lights;
    for (int i = 0; i < 20; ++i) {
        lights.push_back(light_at(120.0f - static_cast<float>(i) * 5.0f, 3.0f));
    }
    lights.push_back(light_at(200.0f, 190.0f));  // a long beam reaching the camera
    render_light dark = light_at(1.0f, 3.0f);
    dark.color = glm::vec3(0.0f);
    lights.push_back(dark);
    const std::vector<render_light> nearest = nearest_scene_lights(lights, glm::vec3(0.0f));
    REQUIRE(nearest.size() == max_scene_lights);
    CHECK(nearest[0].position.x == 200.0f);
    CHECK(nearest[1].position.x == 25.0f);
    CHECK(nearest[15].position.x == 95.0f);
    CHECK(std::none_of(nearest.begin(), nearest.end(), [](const render_light& light) { return light.position.x == 1.0f; }));

    // Fewer lights than fit are all kept: of those reaching the camera the
    // nearer first, in their order when equally near.
    const std::vector<render_light> few =
        nearest_scene_lights({light_at(2.0f, 5.0f), light_at(-1.0f, 5.0f), light_at(1.0f, 5.0f)}, glm::vec3(0.0f));
    REQUIRE(few.size() == 3U);
    CHECK(few[0].position.x == -1.0f);
    CHECK(few[1].position.x == 1.0f);
    CHECK(few[2].position.x == 2.0f);
    CHECK(nearest_scene_lights({}, glm::vec3(0.0f)).empty());
}

TEST_CASE("the haze takes the course's colour in the time of day's tint, full by the play area's edge") {
    course_backdrop backdrop;
    backdrop.haze_color = glm::vec3(0.8f, 0.8f, 0.6f);
    backdrop.haze_amount = 0.5f;
    backdrop.haze_distance = 800.0f;
    const glm::vec3 eye(4.0f, 5.0f, 6.0f);
    const scene_haze noon = make_scene_haze(backdrop, 0.8f, eye, 300.0f, glm::vec3(1.0f));
    CHECK(noon.color == backdrop.haze_color);
    CHECK(near(noon.far_amount, 0.4f));
    CHECK(noon.full_distance == 300.0f);
    CHECK(noon.eye == eye);
    const scene_haze night = make_scene_haze(backdrop, 0.8f, eye, 0.0f, glm::vec3(0.1f, 0.1f, 0.2f));
    CHECK(near(night.color, glm::vec3(0.08f, 0.08f, 0.12f), 0.0001f));
    CHECK(night.full_distance == 800.0f);
    backdrop.haze_amount = 4.0f;
    CHECK(make_scene_haze(backdrop, 0.8f, eye, 0.0f, glm::vec3(1.0f)).far_amount == 1.0f);
    backdrop.haze_distance = 0.0f;
    CHECK(make_scene_haze(backdrop, 0.8f, eye, 0.0f, glm::vec3(1.0f)).full_distance == 1.0f);
}
