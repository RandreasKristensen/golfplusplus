#include "game/way_sign.h"

#include "game/cart_path.h"
#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <utility>

#include <glm/geometric.hpp>

namespace {
// The cart paths as one graph: every joint a node (joints at one spot, where
// paths meet, one node), each path's consecutive joints joined.
struct path_graph {
    std::vector<glm::vec3> nodes;
    std::vector<std::set<std::size_t>> links;
};

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

// The nodes from `from` to `to` on the shortest way along the paths, if any.
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

// The horizontal unit vector from `from` towards `to`, or `fallback`.
glm::vec3 direction_towards(const glm::vec3& from, const glm::vec3& to, const glm::vec3& fallback) {
    return safe_normalize(horizontal(to - from), fallback);
}

glm::vec3 right_of(const glm::vec3& forward) {
    return -yaw_left(forward);
}

// What every sign is placed with.
struct placing {
    const play_area& area;
    const way_sign_tuning& tuning;
    float path_clearance = 0.0f;
    std::vector<way_sign>& placed;
};

// A sign for `to_hole` pointing `points` beside the way through `at`: its
// post `beside` out from there on the `away` side, else the other, as near
// as keeps it clear of the paths and of the signs already placed. With no
// such spot it stands at the first only when `needed`.
void place_beside(const placing& with, const glm::vec3& at, const glm::vec3& points, const glm::vec3& away, const float beside,
                  const std::size_t to_hole, const bool needed) {
    const way_sign_tuning& tuning = with.tuning;
    way_sign sign;
    sign.to_hole = to_hole;
    sign.points = points;
    sign.board_half_width = tuning.board_width * 0.5f;
    sign.board_half_height = tuning.board_height * 0.5f;
    sign.board_thickness = tuning.board_thickness;
    sign.post_radius = tuning.post_radius;
    // The board is fixed to the post's front, so the post never crosses its face.
    const float to_board = sign.board_thickness * 0.5f + sign.post_radius;
    // Another sign's post no nearer than a board and its tip.
    const float spacing = 2.0f * tuning.board_width;
    const auto clear_at = [&](const glm::vec3& foot, const glm::vec3& side) {
        const std::vector<cart_path>& paths = with.area.cart_paths;
        if (distance_past_cart_paths(paths, foot) - sign.post_radius < with.path_clearance) {
            return false;
        }
        const glm::vec3 board = foot - side * to_board;
        const glm::vec3 end = points * sign.board_half_width;
        // Its ends, its middle and its pointed tip.
        for (const glm::vec3& point : {board - end, board, board + end, board + end + points * sign.board_half_height}) {
            if (distance_past_cart_paths(paths, point) - sign.board_thickness * 0.5f < with.path_clearance) {
                return false;
            }
        }
        return std::all_of(with.placed.begin(), with.placed.end(),
                           [&](const way_sign& other) { return horizontal_distance(other.post_foot, foot) >= spacing; });
    };
    constexpr float step_out = 0.25f;
    constexpr int steps_out = 16;
    glm::vec3 side = away;
    glm::vec3 foot = at + away * beside;
    bool found = false;
    for (const glm::vec3& way : {away, -away}) {
        for (int k = 0; k <= steps_out && !found; ++k) {
            const glm::vec3 spot = at + way * (beside + step_out * static_cast<float>(k));
            if (clear_at(spot, way)) {
                side = way;
                foot = spot;
                found = true;
            }
        }
    }
    if (!found && !needed) {
        return;
    }
    sign.face_normal = -side;
    sign.post_foot = anchor_on_terrain(with.area, foot);
    sign.board_center = sign.post_foot + sign.face_normal * to_board +
        glm::vec3(0.0f, tuning.board_lift + sign.board_half_height, 0.0f);
    with.placed.push_back(sign);
}

// Off the green round `pin` on the straight way to `tee`: `offset` past its
// edge, and past any bunker or water beyond it, or halfway there.
glm::vec3 past_green(const play_area& area, const glm::vec3& pin, const glm::vec3& tee, const float offset) {
    constexpr float step = 0.5f;
    const glm::vec3 towards = direction_towards(pin, tee, glm::vec3(0.0f, 0.0f, 1.0f));
    const float length = horizontal_distance(pin, tee);
    const auto material = [&](const float along) { return sample_area(area, pin + towards * along).material; };
    float along = 0.0f;
    while (along < length && material(along) == terrain_material::green) {
        along += step;
    }
    along += offset;
    while (along < length && (material(along) == terrain_material::bunker || material(along) == terrain_material::water ||
                              material(along) == terrain_material::green)) {
        along += step;
    }
    return pin + towards * (along < length ? along : 0.5f * length);
}
}

std::vector<way_sign> place_way_signs(const play_area& area, const std::vector<hole_data>& holes,
                                      const way_sign_tuning& tuning, const float path_clearance) {
    std::vector<way_sign> signs;
    const placing with{area, tuning, path_clearance, signs};
    const path_graph graph = graph_of(area.cart_paths);
    float half_width = 0.0f;
    for (const cart_path& path : area.cart_paths) {
        half_width = std::max(half_width, path.half_width);
    }
    const auto near_path = [&](const glm::vec3& point) -> std::optional<std::size_t> {
        if (graph.nodes.empty()) {
            return std::nullopt;
        }
        const std::size_t node = nearest_node(graph, point);
        return horizontal_distance(graph.nodes[node], point) <= tuning.green_reach ? std::optional<std::size_t>(node)
                                                                                   : std::nullopt;
    };
    for (std::size_t i = 0; i + 1 < holes.size(); ++i) {
        const glm::vec3& pin = holes[i].pin_position;
        const glm::vec3& tee = holes[i + 1].tee_position;
        const std::optional<std::size_t> from = near_path(pin);
        const std::optional<std::size_t> to = near_path(tee);
        const std::optional<std::vector<std::size_t>> route =
            from && to ? route_along(graph, *from, *to) : std::nullopt;
        if (!route || route->size() < 2) {
            // No path that way: straight at the tee from just off the green.
            const glm::vec3 at = past_green(area, pin, tee, tuning.green_offset);
            const glm::vec3 points = direction_towards(at, tee, direction_towards(pin, tee, glm::vec3(0.0f, 0.0f, 1.0f)));
            place_beside(with, at, points, right_of(points), tuning.path_offset, i + 1, true);
            continue;
        }
        const std::vector<std::size_t>& nodes = *route;
        // Where the path leaves the green, on its side away from the pin.
        const glm::vec3 start = graph.nodes[nodes[0]];
        const glm::vec3 first = direction_towards(start, graph.nodes[nodes[1]], glm::vec3(0.0f, 0.0f, 1.0f));
        glm::vec3 away = right_of(first);
        if (glm::dot(horizontal(start - pin), away) < 0.0f) {
            away = -away;
        }
        place_beside(with, start, first, away, half_width + tuning.path_offset, i + 1, true);
        // Before every junction on the way, junction_lead back along the way
        // in, the way to take there, on the side away from the branches not
        // taken.
        for (std::size_t k = 1; k + 1 < nodes.size(); ++k) {
            const std::set<std::size_t>& links = graph.links[nodes[k]];
            if (links.size() < 3) {
                continue;
            }
            const glm::vec3 junction = graph.nodes[nodes[k]];
            std::size_t lead = k;
            while (lead > 0 && horizontal_distance(graph.nodes[nodes[lead]], junction) < tuning.junction_lead) {
                --lead;
            }
            const glm::vec3 at = graph.nodes[nodes[lead]];
            const glm::vec3 points = direction_towards(junction, graph.nodes[nodes[k + 1]], first);
            glm::vec3 others(0.0f);
            for (const std::size_t link : links) {
                if (link != nodes[k - 1] && link != nodes[k + 1]) {
                    others += direction_towards(junction, graph.nodes[link], points);
                }
            }
            const glm::vec3 way_in = direction_towards(at, junction, points);
            glm::vec3 side = right_of(way_in);
            if (glm::dot(others, side) > 0.0f) {
                side = -side;
            }
            place_beside(with, at, points, side, half_width + tuning.path_offset, i + 1, false);
        }
    }
    return signs;
}

float way_sign_top(const way_sign& sign) {
    return sign.board_center.y + sign.board_half_height;
}
