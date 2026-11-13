#include "game/way_sign.h"

#include "game/cart_path.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/tee_box.h"
#include "physics/det_math.h"
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
#include <glm/trigonometric.hpp>

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

// The sign for `to_hole` on a post at `foot`, pointing `points`: its face
// square to that, turned towards `facing`, and fixed to the post's front so
// the post never crosses it.
way_sign sign_at(const placing& with, const glm::vec3& foot, const glm::vec3& points, const glm::vec3& facing,
                 const std::size_t to_hole) {
    const way_sign_tuning& tuning = with.tuning;
    way_sign sign;
    sign.to_hole = to_hole;
    sign.points = points;
    sign.face_normal = right_of(points);
    if (glm::dot(sign.face_normal, horizontal(facing)) < 0.0f) {
        sign.face_normal = -sign.face_normal;
    }
    sign.board_half_width = tuning.board_width * 0.5f;
    sign.board_half_height = tuning.board_height * 0.5f;
    sign.board_thickness = tuning.board_thickness;
    sign.post_radius = tuning.post_radius;
    sign.post_foot = anchor_on_terrain(with.area, foot);
    sign.board_center = sign.post_foot + sign.face_normal * (sign.board_thickness * 0.5f + sign.post_radius) +
        glm::vec3(0.0f, tuning.board_lift + sign.board_half_height, 0.0f);
    return sign;
}

// Off the rough or on a tee box: where play goes.
bool in_play(const play_area& area, const glm::vec3& point) {
    return sample_area(area, point).material != terrain_material::rough ||
        std::any_of(area.tee_boxes.begin(), area.tee_boxes.end(), [&](const tee_box& box) { return on_tee_box(box, point); });
}

// How far `point` is from the nearest tee box.
float from_tees(const play_area& area, const glm::vec3& point) {
    float nearest = std::numeric_limits<float>::max();
    for (const tee_box& box : area.tee_boxes) {
        nearest = std::min(nearest, distance_from_tee_box(box, point));
    }
    return nearest;
}

// Whether `sign` stands out of play, `tee_clearance` from every tee box,
// `path_clearance` clear of the paths, and a board and its tip from every
// sign already placed.
bool stands_clear(const placing& with, const way_sign& sign) {
    const std::vector<cart_path>& paths = with.area.cart_paths;
    if (in_play(with.area, sign.post_foot) || from_tees(with.area, sign.post_foot) < with.tuning.tee_clearance ||
        distance_past_cart_paths(paths, sign.post_foot) - sign.post_radius < with.path_clearance) {
        return false;
    }
    const glm::vec3 end = sign.points * sign.board_half_width;
    const glm::vec3& board = sign.board_center;
    // Its ends, its middle and its pointed tip.
    for (const glm::vec3& point : {board - end, board, board + end, board + end + sign.points * sign.board_half_height}) {
        if (in_play(with.area, point) ||
            distance_past_cart_paths(paths, point) - sign.board_thickness * 0.5f < with.path_clearance) {
            return false;
        }
    }
    const float spacing = 2.0f * with.tuning.board_width;
    return std::all_of(with.placed.begin(), with.placed.end(),
                       [&](const way_sign& other) { return horizontal_distance(other.post_foot, sign.post_foot) >= spacing; });
}

// Where a sign behind a green points: at `target`, or along `then` once there.
struct heading {
    glm::vec3 target{0.0f};
    glm::vec3 then{0.0f, 0.0f, 1.0f};
};

glm::vec3 points_from(const heading& way, const glm::vec3& foot, const float near) {
    return horizontal_distance(foot, way.target) > near ? direction_towards(foot, way.target, way.then) : way.then;
}

// Behind the green round `pin`, on the far side from `in` where it is played
// from: `green_offset` past its edge and past any bunker or water beyond,
// stepped out up to `behind_reach` further until it stands clear, straight
// behind first, then turned up to `behind_spread` either side. With no clear
// spot it stands at the one furthest from the tees.
void place_behind_green(const placing& with, const glm::vec3& pin, const glm::vec3& in, const heading& way,
                        const std::size_t to_hole) {
    const way_sign_tuning& tuning = with.tuning;
    constexpr float step = 0.5f;
    constexpr float green_limit = 60.0f;
    const glm::vec3 behind = direction_towards(in, pin, way.then);
    const auto material = [&](const glm::vec3& point) { return sample_area(with.area, point).material; };
    const int steps_out = static_cast<int>(tuning.behind_reach / step);
    const float spread = glm::radians(tuning.behind_spread);
    std::optional<way_sign> fallback;
    float fallback_from_tees = -1.0f;
    for (const float turn : {0.0f, 0.5f * spread, -0.5f * spread, spread, -spread}) {
        const glm::vec3 out = safe_normalize(behind * det_cos(turn) + right_of(behind) * det_sin(turn), behind);
        float along = 0.0f;
        while (along < green_limit && material(pin + out * along) == terrain_material::green) {
            along += step;
        }
        along += tuning.green_offset;
        while (along < green_limit + tuning.green_offset) {
            const terrain_material here = material(pin + out * along);
            if (here != terrain_material::bunker && here != terrain_material::water && here != terrain_material::green) {
                break;
            }
            along += step;
        }
        for (int k = 0; k <= steps_out; ++k) {
            const glm::vec3 foot = pin + out * (along + step * static_cast<float>(k));
            const way_sign sign = sign_at(with, foot, points_from(way, foot, tuning.board_width), pin - foot, to_hole);
            if (stands_clear(with, sign)) {
                with.placed.push_back(sign);
                return;
            }
            const float sign_from_tees = from_tees(with.area, sign.post_foot);
            if (sign_from_tees > fallback_from_tees) {
                fallback = sign;
                fallback_from_tees = sign_from_tees;
            }
        }
    }
    with.placed.push_back(*fallback);
}

// A sign for `to_hole` pointing `points` beside the way through `at`, come
// to along `way_in`: its post `beside` out from there on the `away` side,
// else the other, as near as stands clear; its face towards the way and back
// along the way in. None where no spot is clear.
void place_beside(const placing& with, const glm::vec3& at, const glm::vec3& points, const glm::vec3& way_in,
                  const glm::vec3& away, const float beside, const std::size_t to_hole) {
    constexpr float step_out = 0.25f;
    constexpr int steps_out = 16;
    for (const glm::vec3& side : {away, -away}) {
        for (int k = 0; k <= steps_out; ++k) {
            const glm::vec3 foot = at + side * (beside + step_out * static_cast<float>(k));
            const way_sign sign = sign_at(with, foot, points, -side - way_in, to_hole);
            if (stands_clear(with, sign)) {
                with.placed.push_back(sign);
                return;
            }
        }
    }
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
        return horizontal_distance(graph.nodes[node], point) <= tuning.path_reach ? std::optional<std::size_t>(node)
                                                                                  : std::nullopt;
    };
    for (std::size_t i = 0; i + 1 < holes.size(); ++i) {
        const glm::vec3& pin = holes[i].pin_position;
        const glm::vec3& tee = holes[i + 1].tee_position;
        const std::vector<glm::vec3> line = line_of_play(holes[i]);
        const glm::vec3& in = line[line.size() - 2];
        const std::optional<std::size_t> from = near_path(pin);
        const std::optional<std::size_t> to = near_path(tee);
        const std::optional<std::vector<std::size_t>> route =
            from && to ? route_along(graph, *from, *to) : std::nullopt;
        const glm::vec3 to_tee = direction_towards(pin, tee, glm::vec3(0.0f, 0.0f, 1.0f));
        if (!route || route->size() < 2) {
            // No path that way: straight at the tee.
            place_behind_green(with, pin, in, heading{tee, to_tee}, i + 1);
            continue;
        }
        const std::vector<std::size_t>& nodes = *route;
        // At where the path leaves the green, then along it.
        const glm::vec3 start = graph.nodes[nodes[0]];
        const glm::vec3 first = direction_towards(start, graph.nodes[nodes[1]], to_tee);
        place_behind_green(with, pin, in, heading{start, first}, i + 1);
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
            place_beside(with, at, points, way_in, side, half_width + tuning.path_offset, i + 1);
        }
    }
    return signs;
}

float way_sign_top(const way_sign& sign) {
    return sign.board_center.y + sign.board_half_height;
}
