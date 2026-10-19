#pragma once

// Draws every blob shadow (render_blob_shadow) in one instanced draw over
// the ground: the trees' at the front of one instance buffer, uploaded when
// their revision changes, this frame's after them. Blended, depth-tested but
// never written, and pulled forward in depth past the drawn zone shapes so
// it lies on whatever ground it falls on.

#include "profiling/profiling.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

class blob_shadow_renderer {
public:
    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();
    // `trees` may be null (none).
    void draw(const std::vector<render_blob_shadow>* trees, std::uint64_t trees_revision,
              const std::vector<render_blob_shadow>& frame, const glm::mat4& view_proj, frame_profile* profile);

private:
    shader_program shader_;
    unsigned int vao_ = 0;
    unsigned int corner_vbo_ = 0;
    unsigned int instance_vbo_ = 0;
    std::size_t capacity_ = 0;  // instances the buffer holds
    std::size_t tree_count_ = 0;
    std::uint64_t trees_revision_ = 0;
    bool trees_uploaded_ = false;
};
