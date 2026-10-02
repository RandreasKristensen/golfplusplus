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
    glm::vec3 color = glm::vec3(1.0f);
    float rough = 0.0f;  // 1 where the rough's grass texture shows, 0 where not
};

// Axis-aligned bounds over every vertex position. `valid` is false for an
// empty vertex list, in which case min/max stay at zero.
struct render_mesh_bounds {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);
    bool valid = false;
};

// A contiguous run of whole triangles in render_static_mesh::indices plus the
// bounds of the vertices those triangles reference. Built once per mesh
// revision (see renderer/render_mesh_chunks.h) and frustum culled per frame.
struct render_mesh_chunk {
    render_mesh_bounds bounds;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

struct render_static_mesh {
    std::vector<render_terrain_vertex> vertices;
    std::vector<std::uint32_t> indices;
    // Recompute with compute_render_mesh_bounds whenever `vertices` changes.
    render_mesh_bounds bounds;
    // Rebuild with build_render_mesh_chunks whenever `vertices`/`indices`
    // change. Empty means "not chunked": the renderer draws the whole mesh.
    std::vector<render_mesh_chunk> chunks;
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
