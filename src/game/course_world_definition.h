#pragma once

// A course's walkable hub (assets/course_worlds/*.json), in shared course
// coordinates. The tooling also writes metadata (OSM refs, projection,
// spawn, walking_shortcuts, spawn_zones, interactables) that the game does not
// read.

#include <string>
#include <vector>

#include <glm/vec3.hpp>

// Where a hole begins. The hole is placed so its tee lands on `position`,
// rotated by `rotation_degrees` around the tee. Holes are tee-relative, so
// `position.y` is what lines their heights up with each other in the hub.
struct course_world_hole_start {
    int hole_index = -1;
    glm::vec3 position{0.0f};
    glm::vec3 return_position{0.0f};  // where the player reappears after the hole
    float interaction_radius = 0.0f;
    float rotation_degrees = 0.0f;
};

struct course_world_cart_road {
    float width = 0.0f;
    std::vector<glm::vec3> polyline;
};

struct course_world_skill_reward {
    std::string skill_id;
    int xp = 0;
};

// Every non-empty field must be met for the collectible to be available.
struct course_world_collectible_requirement {
    std::string skill_id;
    int min_level = 1;
    std::string required_world_flag;
    std::string required_completed_course_id;
};

struct course_world_collectible {
    std::string id;
    glm::vec3 position{0.0f};
    float interaction_radius = 0.0f;
    bool repeatable = false;
    // Holes the player must complete before a repeatable one can be claimed again.
    int repeatable_cooldown_holes = 0;
    std::string world_flag;  // set when claimed
    course_world_collectible_requirement requirement;
    std::vector<course_world_skill_reward> skill_rewards;
};

struct course_world_definition {
    std::string id;
    std::string name;
    // One per hole, in hole order (hole_starts[i].hole_index == i). A course
    // begins at hole 1's start.
    std::vector<course_world_hole_start> hole_starts;
    std::vector<course_world_cart_road> cart_roads;
    std::vector<course_world_collectible> collectibles;
};
