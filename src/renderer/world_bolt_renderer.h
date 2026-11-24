#pragma once

// Draws a real lightning strike's bolt in the scene (renderer/world_bolt.h):
// emissive ribbons added over the picture, depth-tested but never written, so
// the ground and trees hide what is behind them. Far past white for the
// camcorder to bloom and smear, like the sky's bolt.

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/gl_storage.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"
#include "renderer/world_bolt.h"

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

class world_bolt_renderer {
public:
    explicit world_bolt_renderer(gl_storage storage = {})
        : shader_(storage), buffer_(storage) {}

    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();
    void draw(const render_lightning& lightning, const glm::vec3& camera, const glm::mat4& view_proj, frame_profile* profile);

private:
    shader_program shader_;
    dynamic_vertex_buffer buffer_;
    unsigned int vao_ = 0;
    std::vector<world_bolt_vertex> vertices_;
};
