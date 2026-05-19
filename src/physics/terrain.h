#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "physics/material_zone.h"

struct terrain_spline {
    std::vector<glm::vec3> control_points;
    float width = 0.0f;
    int sample_count = 96;
};

enum class terrain_material {
    fairway,
    rough,
    green,
    bunker,
    water
};

struct terrain_zone_tuning {
    float bunker_depth = 0.55f;
    float water_depth = 0.35f;
};

struct terrain_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance_from_center = 0.0f;
    terrain_material material = terrain_material::fairway;
};

struct terrain_mesh {
    std::vector<terrain_vertex> vertices;
    std::vector<uint32_t> indices;
    int section_count = 0;
    int cross_section_count = 0;
    float width = 0.0f;
};

struct terrain_sample {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 barycentric{0.0f};
    float distance_from_center = 0.0f;
    int triangle_index = -1;
    terrain_material material = terrain_material::rough;
    bool has_spline = false;
    bool inside_surface = false;
};

glm::vec3 sample_terrain_spline_point(const terrain_spline& terrain, float t);
terrain_mesh build_terrain_mesh(const terrain_spline& terrain);
terrain_mesh build_terrain_mesh(const terrain_spline& terrain,
                                const std::vector<material_zone>& zones,
                                const terrain_zone_tuning& tuning);
terrain_sample sample_terrain_mesh(const terrain_mesh& mesh, const glm::vec3& position, float fallback_y);
terrain_sample sample_terrain(const terrain_spline& terrain, const glm::vec3& position, float fallback_y);
