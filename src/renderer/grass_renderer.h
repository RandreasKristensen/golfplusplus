#pragma once

// The rough's blades round the eye, in rings (renderer/grass_field.h). For
// each ring the ground round the eye is drawn from above into a small map
// (its height and how grassy it is, the rough's share), again only when the
// eye has moved far enough or the course changes; then the tufts of the
// blocks the eye may see on the ring's world-fixed grid are drawn
// instanced, standing on that map, as tall as the grass there and thinning
// into the next ring out, bent by the wind and lit and hazed like the
// ground.

#include <array>
#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "profiling/profiling.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/grass_field.h"
#include "renderer/render_mesh.h"
#include "renderer/shader.h"

class shadow_map_pass;
class terrain_renderer;

// What the blades are drawn with this frame.
struct grass_frame {
    const render_static_mesh* terrain = nullptr;
    const render_static_mesh* material_overlay = nullptr;
    const std::vector<tee_box>* tee_boxes = nullptr;  // no grass grows through them
    glm::mat4 view_proj{1.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 rough_color{0.0f};
    glm::vec2 wind{0.0f};  // m/s, x and z
    float clock_seconds = 0.0f;
};

class grass_renderer {
public:
    bool init(const std::string& map_vertex, const std::string& map_fragment, const std::string& blade_vertex,
              const std::string& blade_fragment);
    void shutdown();

    // Maps the ground again if needed (drawn by `ground`), then draws the
    // blades into the bound framebuffer, whose binding and viewport it
    // leaves as it found them.
    void draw(const grass_frame& frame, const grass_style& style, terrain_renderer& ground, const shadow_map_pass& shadows,
              int shadow_unit, gl_timer_pool& timers, frame_profile* profile);

private:
    struct ring_map {
        framebuffer map;
        grass_window window;
        std::uint64_t revision = 0;
        bool mapped = false;
    };

    void map_ground(const grass_frame& frame, ring_map& ring, float reach, terrain_renderer& ground, frame_profile* profile);

    shader_program map_shader_;
    shader_program blade_shader_;
    std::array<ring_map, grass_max_rings> rings_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int tee_vao_ = 0;  // the tee boxes' tops, bare, for the map
    unsigned int tee_vbo_ = 0;
    int vertex_count_ = 0;
};
