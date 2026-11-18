#include "game/cart_path.h"

#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <utility>

#include <glm/geometric.hpp>

namespace {
// The road's points every `piece_length` or less along it, each of its own
// points kept (so roads meeting at a point meet at a joint).
std::vector<glm::vec3> joints_along(const std::vector<glm::vec3>& polyline, const float piece_length) {
    std::vector<glm::vec3> out;
    for (std::size_t i = 0; i + 1 < polyline.size(); ++i) {
        const glm::vec3 a = horizontal(polyline[i]);
        const glm::vec3 b = horizontal(polyline[i + 1]);
        const int pieces = std::max(1, static_cast<int>(std::ceil(glm::length(b - a) / piece_length)));
        for (int k = 0; k < pieces; ++k) {
            out.push_back(a + (b - a) * (static_cast<float>(k) / static_cast<float>(pieces)));
        }
    }
    if (!polyline.empty()) {
        out.push_back(horizontal(polyline.back()));
    }
    return out;
}

// The surface under `point`: the ground, or a pond's surface over its water.
float surface_at(const play_area& area, const glm::vec3& point) {
    const terrain_sample sample = sample_area(area, point);
    return sample.material == terrain_material::water ? std::max(sample.point.y, sample.water_level) : sample.point.y;
}
}

std::vector<cart_path> lay_cart_paths(const play_area& area, const std::vector<course_world_cart_road>& roads,
                                      const cart_path_tuning& tuning) {
    std::vector<cart_path> paths;
    for (const course_world_cart_road& road : roads) {
        cart_path path;
        path.half_width = road.width * 0.5f;
        path.joints = joints_along(road.polyline, tuning.piece_length);
        if (path.joints.size() < 2) {
            continue;
        }
        for (glm::vec3& joint : path.joints) {
            joint.y = surface_at(area, joint) + tuning.top_lift;
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

float distance_past_cart_paths(const std::vector<cart_path>& paths, const glm::vec3& point) {
    float nearest = std::numeric_limits<float>::max();
    const glm::vec3 at = horizontal(point);
    for (const cart_path& path : paths) {
        for (std::size_t i = 0; i + 1 < path.joints.size(); ++i) {
            const glm::vec3 a = horizontal(path.joints[i]);
            const glm::vec3 ab = horizontal(path.joints[i + 1]) - a;
            const float length_squared = glm::dot(ab, ab);
            const float t = length_squared <= 1.0e-8f ? 0.0f : clamp01(glm::dot(at - a, ab) / length_squared);
            nearest = std::min(nearest, glm::length(at - (a + ab * t)) - path.half_width);
        }
    }
    return nearest;
}

path_graph graph_of(const std::vector<cart_path>& paths) {
    path_graph graph;
    std::map<std::pair<long, long>, std::size_t> at;
    const auto node = [&](const glm::vec3& joint) {
        const std::pair<long, long> key{std::lround(joint.x * 10.0f), std::lround(joint.z * 10.0f)};
        const auto [found, added] = at.emplace(key, graph.nodes.size());
        if (added) {
            graph.nodes.push_back(joint);
            graph.links.emplace_back();
        }
        return found->second;
    };
    for (const cart_path& path : paths) {
        for (std::size_t i = 0; i + 1 < path.joints.size(); ++i) {
            const std::size_t a = node(path.joints[i]);
            const std::size_t b = node(path.joints[i + 1]);
            if (a != b) {
                graph.links[a].insert(b);
                graph.links[b].insert(a);
            }
        }
    }
    return graph;
}

std::size_t nearest_node(const path_graph& graph, const glm::vec3& point) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < graph.nodes.size(); ++i) {
        if (horizontal_distance(graph.nodes[i], point) < horizontal_distance(graph.nodes[best], point)) {
            best = i;
        }
    }
    return best;
}

std::optional<std::vector<std::size_t>> route_along(const path_graph& graph, const std::size_t from, const std::size_t to) {
    std::vector<float> spent(graph.nodes.size(), std::numeric_limits<float>::max());
    std::vector<std::size_t> came(graph.nodes.size(), from);
    std::priority_queue<std::pair<float, std::size_t>, std::vector<std::pair<float, std::size_t>>, std::greater<>> queue;
    spent[from] = 0.0f;
    queue.emplace(0.0f, from);
    while (!queue.empty()) {
        const auto [cost, node] = queue.top();
        queue.pop();
        if (node == to) {
            std::vector<std::size_t> route{to};
            while (route.back() != from) {
                route.push_back(came[route.back()]);
            }
            std::reverse(route.begin(), route.end());
            return route;
        }
        if (cost > spent[node]) {
            continue;
        }
        for (const std::size_t next : graph.links[node]) {
            const float total = cost + horizontal_distance(graph.nodes[node], graph.nodes[next]);
            if (total < spent[next]) {
                spent[next] = total;
                came[next] = node;
                queue.emplace(total, next);
            }
        }
    }
    return std::nullopt;
}
