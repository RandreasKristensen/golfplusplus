#pragma once

// The small TV the picture is shown on (assets/picture/shaders/crt.frag): the last
// pass, at window resolution, never off. Each scene line is a whole number
// of window rows (renderer/picture_size.h), nearest vertically and soft
// sideways by a fixed share of a UI pixel. Strengths are picture.json's `tv` section.

#include "renderer/gl_storage.h"
#include "game/picture_loader.h"
#include "profiling/profiling.h"
#include "renderer/picture_size.h"
#include "renderer/shader.h"

#include <string>

#include <glm/vec3.hpp>

class tv_pass {
public:
    explicit tv_pass(gl_storage storage = {})
        : shader_(storage) {}

    // `screen_vao` is a full-screen quad (position.xyz, uv) owned by the caller.
    bool init(const std::string& vertex_path, const std::string& fragment_path, unsigned int screen_vao,
              const tv_style& style);
    void shutdown();
    // Shows `picture_texture` (laid out by `picture`) on the default
    // framebuffer, `border` around the curved screen.
    void draw(unsigned int picture_texture, const picture_size& picture, int window_width, int window_height,
              const glm::vec3& border);

private:
    shader_program shader_;
    tv_style style_;
    unsigned int screen_vao_ = 0;
};
