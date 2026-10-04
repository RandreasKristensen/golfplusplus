#pragma once

// The ground the player is on: a whole course, with every hole placed where
// its course world puts it on the course's land, or one hole on its own for a
// course without a world. Built from content, never saved.

#include "game/course_world_definition.h"
#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "physics/terrain.h"
#include "physics/tree_collision.h"
#include "profiling/profiling.h"

#include <vector>

#include <glm/vec3.hpp>

struct play_area {
    float ground_y = 0.0f;  // height used where there is no terrain
    // XZ centre and half-size of the terrain and ground, for the camera's far
    // plane, the backdrop ground and the course map.
    glm::vec3 center{0.0f};
    float extent = 0.0f;
    std::vector<tree_instance> trees;
    // Each hole's ribbon: where the ground takes hole heights from, and the
    // fairway or rough at any point. Never drawn or sampled for height directly.
    std::vector<terrain_mesh> holes;
    // Every hole's greens, bunkers and water, placed like the holes. Their
    // exact shapes decide the material, on a ribbon or off it.
    std::vector<material_zone> zones;
    terrain_mesh ground;            // the one surface (physics/ground_mesh.h)
    terrain_mesh material_overlay;  // render-only zone shapes draped over the ground
};

// Hole-space position -> course position for a hole placed at `start`
// (its tee lands on start.position, rotated around the tee).
glm::vec3 place_hole_point(const hole_data& hole, const course_world_hole_start& start, const glm::vec3& point);
hole_data place_hole(const hole_data& hole, const course_world_hole_start& start);

// One hole on its own, in the coordinates of `hole`.
play_area build_hole_area(const hole_data& hole, const game_tuning& tuning);

// The whole course: every hole placed by its hole start (`holes[i]` belongs to
// `world.hole_starts[i]`), on the world's land. Each hole's terrain is built
// in hole space before placing, so bounds-based zones shape it the same way.
play_area build_course_area(const std::vector<hole_data>& holes,
                            const course_world_definition& world,
                            const game_tuning& tuning);

// The ground's height and normal at `position`, with the material there
// (surface_material: zones, else the holes' fairway or rough, else rough).
terrain_sample sample_area(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
float terrain_height(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
// `position` moved onto the terrain surface, keeping its XZ.
glm::vec3 anchor_on_terrain(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
// Where a ball of `radius` rests on the ground at `position` (XZ).
glm::vec3 resting_ball_position(const play_area& area, const glm::vec3& position, float radius);

// Where the player stands at a teed ball: `stand_off` behind it on the
// line to `pin`, on the ground.
glm::vec3 tee_stance_position(const play_area& area, const glm::vec3& ball, const glm::vec3& pin, float stand_off);

// True when `position` (XZ) is within reach of one of `roads`: a road's half
// width plus cart.road_reach_margin, and never less than cart.min_road_reach.
bool on_cart_road(const std::vector<course_world_cart_road>& roads, const glm::vec3& position, const cart_tuning& cart);

// The area's trees standing on its terrain, as shots hit them.
std::vector<tree_body> standing_trees(const play_area& area, frame_profile* profile = nullptr);
