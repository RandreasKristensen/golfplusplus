#pragma once

// Draws the fences (renderer/fence_batch.h) into the scene, lit and hazed
// like the terrain: the posts solid with the rest of the scene, then the nets
// blended over it without writing depth, so what stands behind a net shows
// through its holes. Uploads only when the fences are rebuilt.

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/fence_batch.h"
#include "renderer/scene_haze.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

class fence_renderer {
public:
    // `net_image` is the net texture's file (assets/textures/fence_net.bmp).
    bool init(const std::string& vertex_path, const std::string& fragment_path, const std::string& net_image);
    void shutdown();

    // The posts; draw with the solid scene. Nothing for null or no fences.
    void draw_posts(const render_fences* fences, const glm::mat4& view_proj, const glm::vec3& light_direction,
                    const scene_haze& haze, frame_profile* profile);
    // The nets; draw after everything solid. Leaves blending off and depth
    // writes on.
    void draw_nets(const render_fences* fences, const glm::mat4& view_proj, const glm::vec3& light_direction,
                   const scene_haze& haze, frame_profile* profile);

private:
    bool begin(const render_fences* fences, const glm::mat4& view_proj, const glm::vec3& light_direction,
               const scene_haze& haze, frame_profile* profile);

    shader_program shader_;
    unsigned int vao_ = 0;
    dynamic_vertex_buffer buffer_;
    texture net_;
    std::size_t post_vertex_count_ = 0;
    std::size_t net_vertex_count_ = 0;
    std::uint64_t uploaded_revision_ = 0;
    bool uploaded_ = false;
};
