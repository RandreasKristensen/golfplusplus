#include "doctest.h"

#include "game/area_solids.h"
#include "game/cart_path.h"
#include "game/play_area.h"
#include "physics/vector_math.h"
#include "renderer/cart_path_mesh.h"

#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

namespace {
const glm::vec3 origin(724.0f, 0.0f, -169.0f);

play_area path_area(const std::vector<course_world_cart_road>& roads) {
    play_area area;
    constexpr int size = 25;
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            terrain_vertex vertex;
            // Folds across cell diagonals expose a top that cuts into the turf.
            vertex.position = origin + glm::vec3(column * 2.0f, row * 0.1f + (column % 3) * 0.15f, row * 2.0f);
            area.ground.vertices.push_back(vertex);
        }
    }
    area.ground.indices = grid_triangle_indices(size, size, 0U);
    area.ground = build_terrain_mesh_index(std::move(area.ground));
    const game_tuning& tuning = shipped_content().tuning;
    area.cart_paths = lay_cart_paths(area, roads, tuning.cart_path);
    area.solids = standing_solids(area, tuning);
    area.solids_grid = build_solid_grid(area.solids, 8.0f);
    return area;
}

float edge_distance(const glm::vec3& point, const std::vector<course_world_cart_road>& roads) {
    float distance = std::numeric_limits<float>::max();
    for (const auto& road : roads) {
        for (std::size_t i = 1; i < road.polyline.size(); ++i) {
            const glm::vec3 a = horizontal(road.polyline[i - 1]);
            const glm::vec3 ab = horizontal(road.polyline[i]) - a;
            const float t = clamp01(glm::dot(horizontal(point) - a, ab) / glm::dot(ab, ab));
            distance = std::min(distance, horizontal_distance(point, a + t * ab) - road.width * 0.5f);
        }
    }
    return distance;
}

// The actual drawn triangle at a spot, including its interpolated height.
std::optional<float> drawn_height(const render_static_mesh& mesh, const glm::vec3& point) {
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const glm::dvec3 a(mesh.vertices[mesh.indices[i]].position);
        const glm::dvec3 b(mesh.vertices[mesh.indices[i + 1]].position);
        const glm::dvec3 c(mesh.vertices[mesh.indices[i + 2]].position);
        const glm::dvec2 u(b.x - a.x, b.z - a.z), v(c.x - a.x, c.z - a.z);
        const glm::dvec2 p(point.x - a.x, point.z - a.z);
        const double determinant = u.x * v.y - u.y * v.x;
        if (std::abs(determinant) < 1.0e-10) {
            continue;
        }
        const double s = (p.x * v.y - p.y * v.x) / determinant;
        const double t = (u.x * p.y - u.y * p.x) / determinant;
        if (s >= -1.0e-6 && t >= -1.0e-6 && s + t <= 1.0 + 1.0e-6) {
            return static_cast<float>(a.y + s * (b.y - a.y) + t * (c.y - a.y));
        }
    }
    return std::nullopt;
}
}

TEST_CASE("cart path mesh keeps its true width at nearly straight joints, bends and junctions") {
    const std::vector<course_world_cart_road> roads{
        {2.5f, {origin + glm::vec3(4.0f, 0.0f, 5.0f), origin + glm::vec3(18.31f, 0.0f, 25.43f),
                 origin + glm::vec3(35.0f, 0.0f, 35.0f), origin + glm::vec3(42.0f, 0.0f, 31.0f)}},
        {2.5f, {origin + glm::vec3(15.0f, 0.0f, 30.0f), origin + glm::vec3(18.31f, 0.0f, 25.43f),
                 origin + glm::vec3(24.0f, 0.0f, 12.0f)}}};
    const play_area area = path_area(roads);
    const render_static_mesh mesh = with_cart_paths({}, area, glm::vec3(0.4f), {});
    REQUIRE(!mesh.indices.empty());
    float outside = 0.0f;
    float lift_error = 0.0f;
    for (const auto index : mesh.indices) {
        const glm::vec3& point = mesh.vertices[index].position;
        outside = std::max(outside, edge_distance(point, roads));
        const float ground = sample_terrain_mesh(area.ground, point, 0.0f).point.y;
        lift_error = std::max(lift_error, std::abs(point.y - ground - shipped_content().tuning.cart_path.top_lift));
    }
    CHECK(outside < 0.002f);
    CHECK(lift_error < 0.001f);
    std::size_t holes = 0, spikes = 0, height_errors = 0;
    // Check both sides of the offset outline, caps, inner bends and a T junction.
    // The 2 cm band allows only the round caps' chord error.
    for (float z = 3.0f; z < 38.0f; z += 0.19f) {
        for (float x = 2.0f; x < 45.0f; x += 0.19f) {
            const glm::vec3 point = origin + glm::vec3(x, 0.0f, z);
            const float edge = edge_distance(point, roads);
            const auto height = drawn_height(mesh, point);
            holes += edge < -0.02f && !height ? 1U : 0U;
            spikes += edge > 0.002f && height ? 1U : 0U;
            if (edge < -0.02f && height) {
                height_errors += std::abs(*height - sample_area(area, point).point.y) > 0.001f ? 1U : 0U;
            }
        }
    }
    // Trace 2 cm inside each exact offset edge as well: narrow grass slivers
    // beside the outline must not slip between the two-dimensional samples.
    for (const auto& road : roads) {
        for (std::size_t i = 1; i < road.polyline.size(); ++i) {
            const glm::vec3 a = road.polyline[i - 1];
            const glm::vec3 span = road.polyline[i] - a;
            const glm::vec3 side = glm::normalize(glm::cross(span, world_up)) * (road.width * 0.5f - 0.02f);
            const int steps = static_cast<int>(std::ceil(glm::length(span) / 0.1f));
            for (int k = 0; k <= steps; ++k) {
                const glm::vec3 at = a + span * (static_cast<float>(k) / steps);
                holes += !drawn_height(mesh, at + side) ? 1U : 0U;
                holes += !drawn_height(mesh, at - side) ? 1U : 0U;
            }
        }
    }
    CHECK(holes == 0U);
    CHECK(spikes == 0U);
    CHECK(height_errors == 0U);
}

TEST_CASE("ground drape rejects degenerate pieces and clips thin triangles by winding") {
    const play_area area = path_area({});
    const glm::vec2 a(origin.x + 10.0f, origin.z + 10.0f);
    const glm::vec2 b = a + glm::vec2(1.25f, 0.0f);
    const glm::vec2 c = b + glm::vec2(0.0f, -0.0000152587890625f);
    CHECK(drape_on_ground(area.ground, {{a, b, b}, {a, a, b}}).empty());
    for (const auto& piece : std::vector<std::array<glm::vec2, 3>>{{a, b, c}, {c, b, a}}) {
        const auto cut = drape_on_ground(area.ground, {piece});
        REQUIRE(!cut.empty());
        for (const auto& vertex : cut) {
            CHECK(vertex.position.x >= a.x - 0.0001f);
            CHECK(vertex.position.x <= b.x + 0.0001f);
            CHECK(vertex.position.z >= c.y - 0.0001f);
            CHECK(vertex.position.z <= a.y + 0.0001f);
        }
    }
}
