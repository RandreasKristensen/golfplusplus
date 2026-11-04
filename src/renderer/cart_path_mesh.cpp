#include "renderer/cart_path_mesh.h"

#include "renderer/render_mesh_chunks.h"
#include "renderer/terrain_render_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
glm::vec2 flat(const glm::vec3& point) {
    return glm::vec2(point.x, point.z);
}

// A cap or a bend's outside is drawn in arcs of at most this (radians).
constexpr float arc_step = glm::pi<float>() / 12.0f;

// A fan of triangles from `centre` round its circle of `radius`, from angle
// `from` turning `by` (radians, either way).
void add_fan(std::vector<std::array<glm::vec2, 3>>& pieces, const glm::vec2& centre, const float radius, const float from,
             const float by) {
    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(by) / arc_step)));
    for (int k = 0; k < steps; ++k) {
        const float a0 = from + by * static_cast<float>(k) / static_cast<float>(steps);
        const float a1 = from + by * static_cast<float>(k + 1) / static_cast<float>(steps);
        pieces.push_back({centre, centre + glm::vec2(std::cos(a0), std::sin(a0)) * radius,
                          centre + glm::vec2(std::cos(a1), std::sin(a1)) * radius});
    }
}

float angle_of(const glm::vec2& v) {
    return std::atan2(v.y, v.x);
}

// The path's footprint as XZ triangles, as its slabs have it: each stretch
// joint to joint, the outside of every bend filled round its joint, and a
// round end at either end, so bends and junctions are whole.
std::vector<std::array<glm::vec2, 3>> footprint(const cart_path& path) {
    std::vector<std::array<glm::vec2, 3>> pieces;
    const float r = path.half_width;
    std::vector<glm::vec2> points;
    for (const glm::vec3& joint : path.joints) {
        if (points.empty() || glm::length(flat(joint) - points.back()) > 1.0e-3f) {
            points.push_back(flat(joint));
        }
    }
    if (points.size() < 2) {
        return pieces;
    }
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const glm::vec2 along = glm::normalize(points[i + 1] - points[i]);
        const glm::vec2 side = glm::vec2(-along.y, along.x) * r;
        pieces.push_back({points[i] - side, points[i + 1] - side, points[i + 1] + side});
        pieces.push_back({points[i] - side, points[i + 1] + side, points[i] + side});
        if (i + 2 < points.size()) {
            // Turning left (positive), the outside is the right: from the right
            // of the way in round to the right of the way out.
            const glm::vec2 out = glm::normalize(points[i + 2] - points[i + 1]);
            const float turn = std::atan2(along.x * out.y - along.y * out.x, glm::dot(along, out));
            const float start = angle_of(turn > 0.0f ? -side : side);
            // Subdivision joints on a straight stretch need no outside fill.
            if (std::abs(turn) > 1.0e-4f) {
                add_fan(pieces, points[i + 1], r, start, turn);
            }
        }
    }
    const glm::vec2 first = glm::normalize(points[1] - points[0]);
    const glm::vec2 last = glm::normalize(points.back() - points[points.size() - 2]);
    add_fan(pieces, points.front(), r, angle_of(glm::vec2(first.y, -first.x)), -glm::pi<float>());
    add_fan(pieces, points.back(), r, angle_of(glm::vec2(-last.y, last.x)), -glm::pi<float>());
    return pieces;
}
}

render_static_mesh with_cart_paths(render_static_mesh mesh, const play_area& area, const glm::vec3& color,
                                   const ground_light_bake& light) {
    // Corners the cut triangles share (to a tenth of a millimetre) are one
    // vertex, sampled once.
    std::map<std::pair<long long, long long>, std::uint32_t> shared;
    const auto vertex = [&](const terrain_vertex& on_ground) {
        const std::pair<long long, long long> key{std::llround(on_ground.position.x * 10000.0f),
                                                  std::llround(on_ground.position.z * 10000.0f)};
        const auto [found, added] = shared.emplace(key, static_cast<std::uint32_t>(mesh.vertices.size()));
        if (added) {
            // The walkable top the game stands on there: over the ground, or a
            // pond's surface where the path bridges it.
            const terrain_sample top = sample_area(area, on_ground.position);
            const bool on_path = top.top_solid >= 0;
            const glm::vec3 at(on_ground.position.x, on_path ? top.point.y : on_ground.position.y, on_ground.position.z);
            mesh.vertices.push_back(lit_ground_vertex(at, on_path ? top.normal : on_ground.normal, color, 0.0f, light));
            mesh.vertices.back().paved = 1.0f;
        }
        return found->second;
    };
    for (const cart_path& path : area.cart_paths) {
        const std::vector<terrain_vertex> cut = drape_on_ground(area.ground, footprint(path));
        for (std::size_t i = 0; i + 2 < cut.size(); i += 3) {
            const std::array<std::uint32_t, 3> corners{vertex(cut[i]), vertex(cut[i + 1]), vertex(cut[i + 2])};
            const glm::vec3& a = mesh.vertices[corners[0]].position;
            // Slivers that sharing corners folds flat or over are left out.
            if (glm::cross(mesh.vertices[corners[1]].position - a, mesh.vertices[corners[2]].position - a).y > 1.0e-8f) {
                mesh.indices.insert(mesh.indices.end(), corners.begin(), corners.end());
            }
        }
    }
    mesh.bounds = compute_render_mesh_bounds(mesh.vertices);
    mesh.chunks = build_render_mesh_chunks(mesh.vertices, mesh.indices);
    return mesh;
}
