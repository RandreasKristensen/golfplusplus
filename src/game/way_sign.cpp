#include "game/way_sign.h"

#include "game/cart_path.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/tee_box.h"
#include "physics/det_math.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
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

// One way to point: along `points`, to these tees.
struct way_to {
    glm::vec3 points{0.0f, 0.0f, 1.0f};
    std::vector<std::size_t> to_holes;
};

// A sign on a post at `foot` with a board for each of `ways`, the first on
// top, each board's blunt end at the post and its pointed end
// way_board_length out. The front of every face is the side towards `facing`.
way_sign sign_at(const placing& with, const glm::vec3& foot, const std::vector<way_to>& ways, const glm::vec3& facing) {
    const way_sign_tuning& tuning = with.tuning;
    way_sign sign;
    sign.board_half_height = tuning.board_height * 0.5f;
    sign.board_thickness = tuning.board_thickness;
    sign.post_radius = tuning.post_radius;
    sign.post_foot = anchor_on_terrain(with.area, foot);
    const float pitch = tuning.board_height + tuning.board_gap;
    for (std::size_t i = 0; i < ways.size(); ++i) {
        way_board board;
        board.to_holes = ways[i].to_holes;
        board.points = ways[i].points;
        board.face_normal = right_of(board.points);
        if (glm::dot(board.face_normal, horizontal(facing)) < 0.0f) {
            board.face_normal = -board.face_normal;
        }
        const float lift = tuning.board_lift + sign.board_half_height + pitch * static_cast<float>(ways.size() - 1 - i);
        board.half_width = 0.5f * (way_board_length(tuning, way_sign_label(board.to_holes)) - sign.board_half_height);
        board.center = sign.post_foot + board.points * board.half_width + glm::vec3(0.0f, lift, 0.0f);
        sign.boards.push_back(std::move(board));
    }
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

// How far its longest board reaches from the post's axis.
float longest_reach(const way_sign& sign) {
    float reach = 0.0f;
    for (const way_board& board : sign.boards) {
        reach = std::max(reach, board.half_width + sign.board_half_height);
    }
    return reach;
}

// Whether `sign` stands out of play, `tee_clearance` from every tee box,
// `path_clearance` clear of the paths, and a post from every sign already
// placed far enough that their boards do not meet.
bool stands_clear(const placing& with, const way_sign& sign) {
    const std::vector<cart_path>& paths = with.area.cart_paths;
    if (in_play(with.area, sign.post_foot) || from_tees(with.area, sign.post_foot) < with.tuning.tee_clearance ||
        distance_past_cart_paths(paths, sign.post_foot) - sign.post_radius < with.path_clearance) {
        return false;
    }
    for (const way_board& board : sign.boards) {
        // Its middle, its far end and its pointed tip.
        for (const glm::vec3& point : {board.center, board.center + board.points * board.half_width, way_board_tip(sign, board)}) {
            if (in_play(with.area, point) ||
                distance_past_cart_paths(paths, point) - sign.board_thickness * 0.5f < with.path_clearance) {
                return false;
            }
        }
    }
    const float reach = longest_reach(sign);
    return std::all_of(with.placed.begin(), with.placed.end(), [&](const way_sign& other) {
        return horizontal_distance(other.post_foot, sign.post_foot) >= reach + longest_reach(other);
    });
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
            const way_sign sign =
                sign_at(with, foot, {way_to{points_from(way, foot, way_board_length(tuning, way_sign_label({to_hole}))), {to_hole}}}, pin - foot);
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

// The branches that leave a junction, each with the tees (indexes into
// play_area::holes) the walks from a green to the next tee take it to.
using branch_tees = std::map<std::size_t, std::set<std::size_t>>;

// One sign at the junction at graph node `node`, a board for each branch with
// tees: its post `beside` out from the junction in a corner between the
// branches, as near as stands clear, trying the widest corner first, and its
// faces towards the junction. None where no corner has a clear spot.
void place_junction(const placing& with, const path_graph& graph, const std::size_t node, const branch_tees& branches,
                    const float beside) {
    constexpr float step_out = 0.5f;
    constexpr int steps_out = 24;
    const glm::vec3 fallback(0.0f, 0.0f, 1.0f);
    const glm::vec3& at = graph.nodes[node];
    std::vector<way_to> ways;
    for (const auto& [branch, holes] : branches) {
        ways.push_back(way_to{direction_towards(at, graph.nodes[branch], fallback), {holes.begin(), holes.end()}});
    }
    std::sort(ways.begin(), ways.end(), [](const way_to& a, const way_to& b) { return a.to_holes.front() < b.to_holes.front(); });

    // Every branch, tees or not, bounds a corner: they are all paths a post must clear.
    std::vector<float> angles;
    for (const std::size_t link : graph.links[node]) {
        const glm::vec3 out = direction_towards(at, graph.nodes[link], fallback);
        angles.push_back(det_atan2(out.z, out.x));
    }
    std::sort(angles.begin(), angles.end());
    struct corner {
        float width = 0.0f;
        glm::vec3 middle{0.0f};
    };
    std::vector<corner> corners;
    for (std::size_t i = 0; i < angles.size(); ++i) {
        const float next = i + 1 < angles.size() ? angles[i + 1] : angles[0] + glm::two_pi<float>();
        const float width = next - angles[i];
        const float middle = angles[i] + 0.5f * width;
        corners.push_back(corner{width, glm::vec3(det_cos(middle), 0.0f, det_sin(middle))});
    }
    std::stable_sort(corners.begin(), corners.end(), [](const corner& a, const corner& b) { return a.width > b.width; });
    for (const corner& between : corners) {
        for (int k = 0; k <= steps_out; ++k) {
            const glm::vec3 foot = at + between.middle * (beside + step_out * static_cast<float>(k));
            const way_sign sign = sign_at(with, foot, ways, at - foot);
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
    // Every junction a walk from a green to the next tee passes, with the
    // tees those walks leave it by each branch for.
    std::map<std::size_t, branch_tees> junctions;
    for (std::size_t i = 0; i + 1 < holes.size(); ++i) {
        const glm::vec3& pin = holes[i].pin_position;
        const glm::vec3& tee = holes[i + 1].tee_position;
        const std::vector<glm::vec3> line = line_of_play(holes[i]);
        place_behind_green(with, pin, line[line.size() - 2], heading{tee, direction_towards(pin, tee, glm::vec3(0.0f, 0.0f, 1.0f))},
                           i + 1);
        const std::optional<std::size_t> from = near_path(pin);
        const std::optional<std::size_t> to = near_path(tee);
        const std::optional<std::vector<std::size_t>> route = from && to ? route_along(graph, *from, *to) : std::nullopt;
        for (std::size_t k = 0; route && k + 1 < route->size(); ++k) {
            if (graph.links[(*route)[k]].size() >= 3) {
                junctions[(*route)[k]][(*route)[k + 1]].insert(i + 1);
            }
        }
    }
    for (const auto& [node, branches] : junctions) {
        place_junction(with, graph, node, branches, half_width + tuning.path_offset);
    }
    return signs;
}

std::string way_sign_label(const std::vector<std::size_t>& to_holes) {
    std::string label;
    for (const std::size_t hole : to_holes) {
        label += (label.empty() ? "" : "  ") + std::to_string(hole + 1);
    }
    return label;
}

float way_board_length(const way_sign_tuning& tuning, const std::string& label) {
    const auto digits = static_cast<float>(std::count_if(label.begin(), label.end(), [](const char c) { return c >= '0' && c <= '9'; }));
    const auto spaces = static_cast<float>(std::count(label.begin(), label.end(), ' '));
    return tuning.board_length + tuning.digit_length * std::max(digits - 1.0f, 0.0f) + tuning.space_length * spaces;
}

glm::vec3 way_board_tip(const way_sign& sign, const way_board& board) {
    return board.center + board.points * (board.half_width + sign.board_half_height);
}

float way_sign_top(const way_sign& sign) {
    return sign.boards.empty() ? sign.post_foot.y : sign.boards.front().center.y + sign.board_half_height;
}
