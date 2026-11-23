#pragma once

// The one lit prop pass: draws prop_batch geometry (renderer/prop_batch.h)
// with assets/picture/shaders/prop.vert / prop.frag, lit by the frame's light
// (lighting.glsl) and the shadow map, the opaque pieces first and the blended
// ones over them. Every piece is an instance of a canonical mesh, placed by
// the vertex shader (renderer/instance_meshes.h): nothing is transformed or
// uploaded per vertex. Three sources share it: the placed models, uploaded once
// per course (set_static, their glow floors by slot: renderer/prop_glow.h);
// the frame's moving models, uploaded once a frame (stage) and drawn by every
// view and the sun's shadow map from that one upload (renderer/prop_frame.h);
// and a small batch streamed for the camera's viewmodel (draw).

#include "renderer/gl_storage.h"
#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/instance_meshes.h"
#include "renderer/prop_batch.h"
#include "renderer/prop_frame.h"
#include "renderer/shader.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

class prop_renderer {
public:
    explicit prop_renderer(gl_storage storage = {})
        : shader_(storage), meshes_(storage), frame_buffer_(storage), static_buffer_(storage), stream_buffer_(storage) {}

    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();

    // Uploads the placed models (glow as slots) unless `revision` is the one already held.
    void set_static(const prop_batch& batch, std::uint64_t revision, frame_profile* profile);
    // Uploads this frame's moving models once; views draw ranges of it.
    void stage(const prop_frame& frame, frame_profile* profile);

    // The caller binds the shadow map to the shader first (shadow_map_pass::bind).
    void draw_static(const glm::mat4& view_proj, const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile);
    void draw_frame(const prop_frame& frame, prop_view view, const glm::mat4& view_proj,
                    const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile);
    // Streams and draws `batch`; nothing for an empty one.
    void draw(const prop_batch& batch, const glm::mat4& view_proj, frame_profile* profile);

    // GL buffers the shadow map reads instances from (mesh_instance layout) and what they hold: the placed
    // models' opaque instances of each mesh, from the start of the static buffer's joined lists.
    unsigned int static_buffer() const { return static_buffer_.id(); }
    const std::array<prop_range, prop_mesh_count>& static_opaque_runs() const { return static_opaque_; }
    unsigned int frame_buffer() const { return frame_buffer_.id(); }
    std::uint64_t static_revision() const { return static_revision_; }

    const shader_program& shader() const { return shader_; }

private:
    void begin(const glm::mat4& view_proj, const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile);
    // `ranges` of each mesh's list, the lists starting at `bases` in `buffer`; blended ones with blending on.
    void draw_ranges(unsigned int buffer, const std::array<std::size_t, prop_mesh_count>& bases, const prop_ranges& ranges,
                     bool blended, frame_profile* profile) const;

    shader_program shader_;
    instance_meshes meshes_;
    dynamic_vertex_buffer frame_buffer_;
    dynamic_vertex_buffer static_buffer_;
    dynamic_vertex_buffer stream_buffer_;
    std::vector<mesh_instance> scratch_;  // an upload's two lists joined
    std::array<prop_range, prop_mesh_count> static_opaque_{};
    std::array<std::array<std::size_t, prop_mesh_count>, 2> static_bases_{};  // [blended][mesh]
    std::array<prop_ranges, 2> static_ranges_{};                              // [blended]
    std::uint64_t static_revision_ = 0;
    bool static_held_ = false;
};
