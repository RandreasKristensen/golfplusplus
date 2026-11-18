#pragma once

// Draws fixed world tufts from the course grass_field (grass_field.h),
// thinning by nested representatives and widening continuously with distance.
// No camera-dependent maps or terrain redraws.

#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "profiling/profiling.h"
#include "renderer/gl_timer.h"
#include "renderer/grass_field.h"
#include "renderer/render_mesh.h"
#include "renderer/shader.h"

class shadow_map_pass;

// What the blades are drawn with this frame.
struct grass_frame {
    const render_static_mesh* terrain = nullptr;
    const render_static_mesh* material_overlay = nullptr;
    const grass_field* field = nullptr;
    glm::mat4 view_proj{1.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 rough_color{0.0f};
    glm::vec2 wind{0.0f};  // m/s, x and z
    float clock_seconds = 0.0f;
};

class grass_renderer {
public:
    bool init(const std::string& blade_vertex, const std::string& blade_fragment, const std::string& defines = {});
    void shutdown();
    // Uploads the course field once per revision; only culling and density
    // selection depend on the eye. Leaves the framebuffer and viewport alone.
    void draw(const grass_frame& frame, const grass_style& style, const shadow_map_pass& shadows,
              int shadow_unit, gl_timer_pool& timers, frame_profile* profile);
private:
    shader_program blade_shader_;
    unsigned int field_texture_ = 0;
    std::uint64_t field_revision_ = 0;
    bool uploaded_ = false;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int vertex_count_ = 0;
};
