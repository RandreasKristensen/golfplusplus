#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

// GL-free render mesh data. Kept separate from renderer.h so the pure helpers
// here can be unit tested without SDL or an OpenGL context.

struct render_terrain_vertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 color = glm::vec3(0.18f, 0.42f, 0.18f);
};

// Axis-aligned bounds over every vertex position. `valid` is false for an
// empty vertex list, in which case min/max stay at zero.
struct render_mesh_bounds {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);
    bool valid = false;
};

struct render_static_mesh {
    std::vector<render_terrain_vertex> vertices;
    std::vector<std::uint32_t> indices;
    // Recompute with compute_render_mesh_bounds whenever `vertices` changes.
    render_mesh_bounds bounds;
    std::uint64_t revision = 0;
};

inline render_mesh_bounds compute_render_mesh_bounds(const std::vector<render_terrain_vertex>& vertices) {
    render_mesh_bounds bounds;
    if (vertices.empty()) {
        return bounds;
    }

    bounds.min = vertices.front().position;
    bounds.max = vertices.front().position;
    for (const render_terrain_vertex& vertex : vertices) {
        bounds.min.x = std::min(bounds.min.x, vertex.position.x);
        bounds.min.y = std::min(bounds.min.y, vertex.position.y);
        bounds.min.z = std::min(bounds.min.z, vertex.position.z);
        bounds.max.x = std::max(bounds.max.x, vertex.position.x);
        bounds.max.y = std::max(bounds.max.y, vertex.position.y);
        bounds.max.z = std::max(bounds.max.z, vertex.position.z);
    }
    bounds.valid = true;
    return bounds;
}

// Lowest mesh height clamped to at most zero; 0 for a missing or empty mesh.
inline float render_mesh_min_y_or_zero(const render_static_mesh* mesh) {
    if (mesh == nullptr || !mesh->bounds.valid) {
        return 0.0f;
    }
    return std::min(0.0f, mesh->bounds.min.y);
}
