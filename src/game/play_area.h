#pragma once

// The ground the player is on: one hole, or a course hub with every hole
// placed where the course world puts it. Built from content, never saved.

#include "game/course_world_definition.h"
#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "physics/terrain.h"
#include "profiling/profiling.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct play_area {
    std::uint32_t wind_seed = 0;
    float ground_y = 0.0f;  // height used where there is no terrain
    // XZ centre and half-size of the terrain (with apron), for the camera's
    // far plane, the backdrop ground and the course map.
    glm::vec3 center{0.0f};
    float extent = 0.0f;
    std::vector<tree_instance> trees;
    terrain_mesh terrain;
    terrain_mesh apron;             // render-only rough around the terrain
    terrain_mesh material_overlay;  // render-only zone shapes draped over the terrain
};

// Hole-space position -> course position for a hole placed at `start`
// (its tee lands on start.position, rotated around the tee).
glm::vec3 place_hole_point(const hole_data& hole, const course_world_hole_start& start, const glm::vec3& point);
hole_data place_hole(const hole_data& hole, const course_world_hole_start& start);

// One hole, in the coordinates of `hole` (use place_hole first to play it
// where a hub shows it). The terrain is built in hole space before placing,
// so bounds-based zones shape it the same either way.
play_area build_hole_area(const hole_data& hole, const game_tuning& tuning);
play_area build_placed_hole_area(const hole_data& hole, const course_world_hole_start& start, const game_tuning& tuning);

// Every hole of a course placed by its hole start, as one area. `holes[i]`
// belongs to `world.hole_starts[i]`.
play_area build_hub_area(const std::vector<hole_data>& holes,
                         const course_world_definition& world,
                         const game_tuning& tuning);

terrain_sample sample_area(const play_area& area,
                           const glm::vec3& position,
                           frame_profile* profile = nullptr,
                           const terrain_sample* previous_sample = nullptr);
float terrain_height(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
// `position` moved onto the terrain surface, keeping its XZ.
glm::vec3 anchor_on_terrain(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
