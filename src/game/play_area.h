#pragma once

// The ground the player is on: a whole course, with every hole placed where
// its course world puts it on the course's land. Built from content, never
// saved.

#include "game/course_world_definition.h"
#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "game/hole_sign.h"
#include "game/tee_box.h"
#include "physics/fence_collision.h"
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
    // The XZ box around every hole's ribbon and zones: the course itself,
    // without the land around it (what the course map fits to its paper).
    glm::vec3 holes_low{0.0f};
    glm::vec3 holes_high{0.0f};
    std::vector<tree_instance> trees;
    // Each hole's ribbon: where the ground takes hole heights from, and the
    // fairway or rough at any point. Never drawn or sampled for height directly.
    std::vector<terrain_mesh> holes;
    // Every hole's greens, bunkers and water, placed like the holes. Their
    // exact shapes decide the material, on a ribbon or off it.
    std::vector<material_zone> zones;
    // Each water zone's surface height (its lowest rim), by index into `zones`;
    // 0 for the others. The ground under it is the pond's bowl.
    std::vector<float> water_levels;
    terrain_mesh ground;            // the one surface (physics/ground_mesh.h)
    terrain_mesh material_overlay;  // render-only zone shapes draped over the ground
    // A flat tee box at each hole's tee, standing on the ground: inside one,
    // its top is the surface (sample_area).
    std::vector<tee_box> tee_boxes;
    // A sign at every hole's tee, and their posts as shots hit them.
    std::vector<hole_sign> signs;
    std::vector<tree_body> sign_posts;
    // The course world's fences with their poles on the ground, and what
    // shots hit: each pole like a trunk, the net between each pair of poles.
    std::vector<course_world_fence> fences;
    std::vector<tree_body> fence_poles;
    std::vector<fence_panel> fence_panels;
};

// Hole-space position -> course position for a hole placed at `start`
// (its tee lands on start.position, rotated around the tee).
glm::vec3 place_hole_point(const hole_data& hole, const course_world_hole_start& start, const glm::vec3& point);
hole_data place_hole(const hole_data& hole, const course_world_hole_start& start);

// The whole course: every hole placed by its hole start (`holes[i]` belongs to
// `world.hole_starts[i]`), on the world's land (none when its grid is empty:
// the holes keep their own heights). Each hole's terrain is built in hole
// space before placing, so turned zones shape it the same way.
play_area build_course_area(const std::vector<hole_data>& holes,
                            const course_world_definition& world,
                            const game_tuning& tuning);

// The ground's height and normal at `position`, with the material there
// (surface_material: zones, else the holes' fairway or rough, else rough).
// On a tee box, its flat top instead of the ground.
terrain_sample sample_area(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
float terrain_height(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
// True below the surface of a pond (a camera looking out from under it).
bool under_water(const play_area& area, const glm::vec3& position);
// `position` moved onto the terrain surface, keeping its XZ.
glm::vec3 anchor_on_terrain(const play_area& area, const glm::vec3& position, frame_profile* profile = nullptr);
// Where a ball of `radius` rests on the ground at `position` (XZ).
glm::vec3 resting_ball_position(const play_area& area, const glm::vec3& position, float radius);

// Where the player stands at a teed ball: `stand_off` behind it on the
// line to `pin`, on the ground.
glm::vec3 tee_stance_position(const play_area& area, const glm::vec3& ball, const glm::vec3& pin, float stand_off);

// Where a player addressing `ball` aimed along `aim_angle` stands: to its
// side and a little behind (tuning.player), on the ground.
glm::vec3 address_stance_position(const play_area& area, const glm::vec3& ball, float aim_angle,
                                  const player_tuning& player, frame_profile* profile = nullptr);

// True when `position` (XZ) is within reach of one of `roads`: a road's half
// width plus cart.road_reach_margin, and never less than cart.min_road_reach.
bool on_cart_road(const std::vector<course_world_cart_road>& roads, const glm::vec3& position, const cart_tuning& cart);

// The area's trees standing on its terrain, as shots hit them.
std::vector<tree_body> standing_trees(const play_area& area, frame_profile* profile = nullptr);
