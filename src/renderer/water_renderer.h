#pragma once

// Draws the ponds (renderer/water_batch.h): their surfaces blended over the
// solid scene, seen from above or below, hazed like the terrain; and, while
// the camera is under one, the scene tinted the murky blue of being in it.
// Uploads the surfaces only when they are rebuilt.

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/scene_haze.h"
#include "renderer/shader.h"
#include "renderer/water_batch.h"

class water_renderer {
public:
    // `screen_vao` is a full-screen quad (position.xyz first) owned by the caller.
    bool init(const std::string& surface_vertex, const std::string& surface_fragment, const std::string& tint_vertex,
              const std::string& tint_fragment, unsigned int screen_vao);
    void shutdown();

    // The surfaces; draw after everything solid. Leaves blending off and depth writes on.
    void draw_surfaces(const render_water* water, const glm::mat4& view_proj, const scene_haze& haze,
                       frame_profile* profile);
    // The underwater tint over the whole scene; draw last, before the HUD.
    void draw_underwater(frame_profile* profile);

private:
    shader_program surface_shader_;
    shader_program tint_shader_;
    unsigned int vao_ = 0;
    unsigned int screen_vao_ = 0;
    dynamic_vertex_buffer buffer_;
    std::size_t vertex_count_ = 0;
    std::uint64_t uploaded_revision_ = 0;
    bool uploaded_ = false;
};
