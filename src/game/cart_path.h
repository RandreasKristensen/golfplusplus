#pragma once

// The course world's cart paths (course_world_cart_road, from the importer's
// one network) laid on the play area: each a run of joints along its middle,
// `piece_length` apart. Between joints lie the slabs of game/area_solids.h,
// whose hard top is `top_lift` over the ground (a pond's surface where a path
// bridges one) and follows it, so it is flush with the turf everywhere, never
// floating or buried, and so with tee pads and bunker lips levelled or carved
// in beside it. It is the one surface's walkable top (sample_area), which the
// ball bounces and rolls on, people walk on and carts drive on with the
// path's numbers (cart_ground_at). Pure: the server lays the same. Drawn by
// renderer/cart_path_mesh.h.

#include "game/course_world_definition.h"
#include "game/game_tuning.h"

#include <cstddef>
#include <optional>
#include <set>
#include <vector>

#include <glm/vec3.hpp>

struct play_area;

struct cart_path {
    float half_width = 0.0f;
    std::vector<glm::vec3> joints;  // along its middle, on its top
};

// `roads` laid on `area`'s ground (built, with nothing standing on it yet).
std::vector<cart_path> lay_cart_paths(const play_area& area, const std::vector<course_world_cart_road>& roads,
                                      const cart_path_tuning& tuning);

// How far `point` is horizontally past the nearest path's edge (negative on
// a path; the float maximum with no paths).
float distance_past_cart_paths(const std::vector<cart_path>& paths, const glm::vec3& point);

// The cart paths as one graph: every joint a node (joints at one spot, where
// paths meet, one node), each path's consecutive joints joined.
struct path_graph {
    std::vector<glm::vec3> nodes;
    std::vector<std::set<std::size_t>> links;
};

path_graph graph_of(const std::vector<cart_path>& paths);

// The node nearest `point` (the graph has at least one).
std::size_t nearest_node(const path_graph& graph, const glm::vec3& point);

// The nodes from `from` to `to` on the shortest way along the paths, if any.
std::optional<std::vector<std::size_t>> route_along(const path_graph& graph, std::size_t from, std::size_t to);
