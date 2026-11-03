#pragma once

// The one lit prop pass: uploads a prop_batch (renderer/prop_batch.h) and
// draws it with assets/picture/shaders/prop.vert / prop.frag, lit by the
// frame's light (lighting.glsl) and the shadow map, the opaque pieces first
// and the blended ones over them. The GPU buffer only ever grows
// (renderer/dynamic_buffer.h).

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/prop_batch.h"
#include "renderer/shader.h"

#include <string>

#include <glm/mat4x4.hpp>

class prop_renderer {
public:
    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();

    // Draws (and uploads) nothing for an empty batch. The caller binds the
    // shadow map to the shader first (shadow_map_pass::bind).
    void draw(const prop_batch& batch, const glm::mat4& view_proj, frame_profile* profile);
    const shader_program& shader() const { return shader_; }

private:
    void draw_vertices(const std::vector<prop_vertex>& vertices, frame_profile* profile);

    shader_program shader_;
    unsigned int vao_ = 0;
    dynamic_vertex_buffer buffer_;
};
