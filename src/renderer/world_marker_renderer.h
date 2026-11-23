#pragma once

#include "renderer/gl_storage.h"
#include <array>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/instance_meshes.h"
#include "renderer/shader.h"
#include "renderer/world_marker_batch.h"

#include <vector>

// Uploads a world_marker_batch into one vertex buffer and, for a renderer made
// for instances, one instance buffer (stage), once a frame or once per course,
// and draws the runs a view shows (marker_view) from them with one draw call per
// run and a single uniform set per view, cutting the cups' holes with the
// stencil the scene was cleared with. The figures' and balls' instances (the
// canonical meshes placed by them: instance_meshes.h) are drawn after the
// vertex runs, one instanced draw per part and mesh. The sun's shadow map
// reads the same buffers (buffer(), instance_buffer()).
//
// The GPU buffer only ever grows (see renderer/dynamic_buffer.h): a frame that
// fits the current capacity is streamed with glBufferSubData; a larger frame
// reallocates (orphans) the storage once before streaming.
class world_marker_renderer {
public:
    explicit world_marker_renderer(gl_storage storage = {})
        : shader_(storage), instanced_shader_(storage), meshes_(storage), buffer_(storage), instance_buffer_(storage) {}

    // `instanced`: the batches staged hold instances too (the vertex shader's INSTANCED variant draws them).
    bool init(const std::string& vertex_path, const std::string& fragment_path, bool instanced = false);
    void shutdown();

    // Uploads `batch`; a batch kept for a whole course is `cached`.
    void stage(const world_marker_batch& batch, frame_profile* profile, upload_kind kind = upload_kind::dynamic);
    // Draws what `view` shows of the staged `batch`. Leaves depth writes
    // enabled and no VAO bound on return.
    // `glow_floors`: this frame's floors by slot (prop_glow.h) for cached pieces.
    void draw(const world_marker_batch& batch, marker_view view, const glm::mat4& view_proj,
              const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile);
    unsigned int buffer() const { return buffer_.id(); }
    unsigned int instance_buffer() const { return instance_buffer_.id(); }
    const shader_program& shader() const { return shader_; }

private:
    shader_program shader_;
    shader_program instanced_shader_;
    instance_meshes meshes_;
    unsigned int vao_ = 0;
    dynamic_vertex_buffer buffer_;
    dynamic_vertex_buffer instance_buffer_;
    std::vector<mesh_instance> scratch_;  // the batch's instance lists joined
};
