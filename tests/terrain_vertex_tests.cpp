#include "doctest.h"

#include "renderer/terrain_vertex.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

TEST_CASE("the ground's vertex is 32 bytes and holds what the shader reads exactly where it matters") {
    static_assert(sizeof(render_terrain_vertex) == 32);
    const render_terrain_vertex fresh;
    CHECK(fresh.normal() == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(fresh.color() == glm::vec3(1.0f));
    CHECK(fresh.ao() == 1.0f);
    CHECK(fresh.turf() == 0.0f);
    CHECK(fresh.blades() == 0.0f);

    render_terrain_vertex v;
    for (const float level : {0.0f, 1.0f, 2.0f}) {
        v.set_turf(level);
        v.set_blades(level);
        CHECK(v.turf() == level);
        CHECK(v.blades() == level);
    }
    v.set_turf(1.5f);
    CHECK(std::abs(v.turf() - 1.5f) < 0.005f);
}

TEST_CASE("an attribute rounds to a fixed step of its value, never to the view") {
    render_terrain_vertex v;
    float worst_normal = 0.0f;
    float worst_unit = 0.0f;
    for (int i = 0; i < 4000; ++i) {
        const float t = static_cast<float>(i) / 4000.0f;
        const glm::vec3 normal = glm::normalize(glm::vec3(std::sin(t * 40.0f), 0.3f + t, std::cos(t * 23.0f)));
        v.set_normal(normal);
        worst_normal = std::max(worst_normal, glm::length(v.normal() - normal));
        v.set_ao(t);
        v.set_rough(t);
        v.set_color(glm::vec3(t, 1.0f - t, t * t));
        worst_unit = std::max({worst_unit, std::abs(v.ao() - t), std::abs(v.rough() - t), std::abs(v.color().z - t * t)});
    }
    CHECK(worst_normal < 0.0018f);  // 0.1 degrees at most
    CHECK(worst_unit <= 0.5f / 255.0f + 1.0e-6f);
    v.set_color(glm::vec3(-1.0f, 2.0f, 0.5f));
    CHECK(v.color().x == 0.0f);
    CHECK(v.color().y == 1.0f);
}
