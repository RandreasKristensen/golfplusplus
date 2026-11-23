#pragma once

// The canonical meshes (mesh_instance.h) in one GL buffer, uploaded once, and
// the instanced draw that places them from a buffer of mesh_instance. Every
// program that draws instances (prop.vert, world_marker.vert with INSTANCED,
// shadow_map.vert with MODEL_CASTER) reads the same attribute slots: the mesh's
// position and normal at 0 and 1, and per instance the matrix's columns at 3 to 6,
// colour and alpha at 7 and the glow floor at 8 (mesh_instance.glsl). OpenGL 3.3:
// the first instance is reached by pointing the attributes at it, not by a base
// instance.

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/gl_storage.h"
#include "renderer/mesh_instance.h"

#include <array>
#include <cstddef>

class instance_meshes {
public:
    explicit instance_meshes(gl_storage storage = {}) : buffer_(storage) {}

    bool init();
    void shutdown();
    // Draws `count` instances of `mesh`, the `first`th of `instances` (a buffer of mesh_instance) on. Leaves no VAO or buffer bound.
    void draw(unsigned instances, prop_mesh mesh, std::size_t first, std::size_t count, frame_profile* profile) const;

private:
    dynamic_vertex_buffer buffer_;
    unsigned int vao_ = 0;
    std::array<int, prop_mesh_count> first_vertex_{};
    std::array<int, prop_mesh_count> vertex_count_{};
};
