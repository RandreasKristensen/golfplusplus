#pragma once

#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct course_world_spawn {
    std::string id;
    glm::vec3 position{0.0f};
    float radius = 0.0f;
};

struct course_world_path {
    std::string id;
    std::string surface;
    std::string source;
    std::string osm_ref;
    float width = 0.0f;
    std::vector<glm::vec3> polyline;
};

struct course_world_hole_start {
    std::string id;
    int hole_index = -1;
    glm::vec3 position{0.0f};
    glm::vec3 return_position{0.0f};
    float interaction_radius = 4.0f;
    float rotation_degrees = 0.0f;
};

struct course_world_skill_reward {
    std::string skill_id;
    int xp = 0;
};

struct course_world_collectible_requirement {
    std::string skill_id;
    int min_level = 1;
    std::string required_world_flag;
    std::string required_completed_course_id;
};

struct course_world_collectible {
    std::string id;
    std::string kind;
    glm::vec3 position{0.0f};
    float interaction_radius = 2.5f;
    bool repeatable = false;
    int repeatable_cooldown_holes = 0;
    std::string world_flag;
    course_world_collectible_requirement requirement;
    std::vector<course_world_skill_reward> skill_rewards;
};

struct course_world_definition {
    std::string id;
    std::string name;
    double origin_lat = 0.0;
    double origin_lon = 0.0;
    course_world_spawn spawn;
    std::vector<course_world_hole_start> hole_starts;
    std::vector<course_world_path> cart_roads;
    std::vector<course_world_collectible> collectibles;
};
