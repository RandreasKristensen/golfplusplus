#pragma once

// What each kind of model (game/prop_loader.h) must have for the code that
// draws it: the colours it reads by name, the points it anchors on, the
// mounts others hang at and the tagged parts it animates. A new model of a
// known kind needs no code; a new kind is added here. GL-free.

#include <string>
#include <vector>

struct model_kind {
    const char* name;
    std::vector<std::string> colors;
    std::vector<std::string> points;
    std::vector<std::string> mounts;
    std::vector<std::string> part_tags;
};

// Every kind there is, by name.
const std::vector<model_kind>& model_kinds();
const model_kind* find_model_kind(const std::string& name);

// The models the code draws by name, not through an item: the course's
// fences, floodlights and hole signs (its trees are grown, renderer/tree_model.h).
inline constexpr const char* fence_model = "fence";
inline constexpr const char* floodlight_model = "floodlight";
inline constexpr const char* hole_sign_model = "hole_sign";
// The hub models whose board the code writes by name (core/tee_book.h
// prop_board_labels): the clocks, a target's distance board, the departure
// board's rows.
inline constexpr const char* clock_model = "hub_clock";
inline constexpr const char* distance_board_model = "hub_distance_board";
inline constexpr const char* departure_board_model = "hub_departure_board";
inline constexpr const char* departure_row_model = "hub_departure_row";
// Every model a hub places or its scene needs, loaded with the hub by game and
// server alike (their bounds are simulation content): the tee book, the bus
// and the built things of the plaza and the range.
struct hub_model {
    const char* id;
    const char* kind;
};
const std::vector<hub_model>& hub_models();
struct required_model {
    const char* id;
    const char* kind;
};
const std::vector<required_model>& required_models();
