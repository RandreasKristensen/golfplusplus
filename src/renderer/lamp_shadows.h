#pragma once

// Which floodlights' beams take shadows this frame, how each one's shadow map
// sees the world and which tile of the maps' texture holds it
// (renderer/lamp_shadow_pass.h draws them). GL-free.

#include "renderer/render_data.h"

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// All a lamp's map depends on: where it is, which way and how wide it
// shines, and how far.
struct lamp_view {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f};
    float cone_edge_cos = 0.0f;
    float radius = 0.0f;

    bool operator==(const lamp_view& other) const;
};

lamp_view view_of(const render_light& light);

// The fixed lamps among `lights` (the scene's, in frame_lighting's order,
// nearest first) whose beams take shadows: at most `count`, in that order,
// each a cone narrower than a half-space reaching within `reach_meters` of
// `eye` (no shaft steps further). Indices into `lights`.
std::vector<std::size_t> shadowed_lamps(const std::vector<render_light>& lights, const glm::vec3& eye,
                                        float reach_meters, std::size_t count);

// Clip space of `lamp`'s map: looking along its beam from `near_meters` in
// front of it (past its own head) out to its reach, square across its cone.
glm::mat4 lamp_shadow_view_proj(const lamp_view& lamp, float near_meters);

// A shadowed lamp this frame: its light in frame_lighting and its map's tile.
struct lamp_slot {
    std::size_t light = 0;
    std::size_t tile = 0;
};

struct lamp_tiling {
    std::vector<std::optional<lamp_view>> held;  // what each tile holds the map of
    std::vector<lamp_slot> slots;                // the chosen lamps', in their order
    std::vector<std::size_t> redraw;             // the tiles given a new lamp
};

// The tiles once `chosen` (shadowed_lamps) of `lights` are placed in tiles
// holding `held`: a lamp a tile already holds keeps it, so the camera moving
// redraws nothing until a new lamp comes near; a new one takes an empty tile,
// else one whose lamp is not chosen. `chosen` is no longer than `held`.
lamp_tiling place_lamps(const std::vector<std::optional<lamp_view>>& held, const std::vector<render_light>& lights,
                        const std::vector<std::size_t>& chosen);
