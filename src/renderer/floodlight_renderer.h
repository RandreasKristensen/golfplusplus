#pragma once

// Draws the floodlights (renderer/floodlight_batch.h) into the scene in one
// draw, lit and hazed like the trees, their lamps' faces glowing in
// lighting.glsl's lamp_glow; and the same mesh into the trees' shadow map.
// Uploads only when the floodlights are rebuilt.

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/floodlight_batch.h"
#include "renderer/shader.h"

class floodlight_renderer {
public:
    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();

    // Nothing for null or no floodlights.
    void draw(const render_floodlights* floodlights, const glm::mat4& view_proj, frame_profile* profile);
    // The mesh with `depth_shader` (already in use, instanced like
    // shadow_map.vert: drawn as one instance at the origin, unscaled) seen
    // through `view_proj`.
    void draw_casters(const render_floodlights* floodlights, const shader_program& depth_shader,
                      const glm::mat4& view_proj, frame_profile* profile);

private:
    // False when there is nothing to draw; else leaves the VAO bound.
    bool prepare(const render_floodlights* floodlights, frame_profile* profile);

    shader_program shader_;
    unsigned int vao_ = 0;
    dynamic_vertex_buffer buffer_;
    std::size_t vertex_count_ = 0;
    std::uint64_t uploaded_revision_ = 0;
    bool uploaded_ = false;
};
