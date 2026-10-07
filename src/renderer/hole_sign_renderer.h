#pragma once

// Draws the hole signs (renderer/hole_sign_batch.h) into the scene: lit and
// hazed like the terrain, each board's face from its own small texture with
// chunky texels up close. Uploads the models and textures only when the
// signs are rebuilt; one draw per sign.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/scene_haze.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

class hole_sign_renderer {
public:
    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();

    // Draws nothing for null or no signs. Leaves no VAO bound on return.
    void draw(const render_hole_signs* signs,
              const glm::mat4& view_proj,
              const glm::vec3& light_direction,
              const scene_haze& haze,
              frame_profile* profile);

private:
    void upload(const render_hole_signs& signs, frame_profile* profile);

    shader_program shader_;
    unsigned int vao_ = 0;
    dynamic_vertex_buffer buffer_;
    std::vector<texture> faces_;
    std::uint64_t uploaded_revision_ = 0;
    bool uploaded_ = false;
};
