#include "renderer/tv_pass.h"

#include "renderer/framebuffer.h"
#include "renderer/gl_loader.h"

#include <algorithm>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr GLsizei screen_quad_vertices = 6;
}

bool tv_pass::init(const std::string& vertex_path, const std::string& fragment_path, const unsigned int screen_vao,
                   const tv_style& style) {
    screen_vao_ = screen_vao;
    style_ = style;
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    shader_.use();
    shader_.set_int("u_scene", 0);
    return true;
}

void tv_pass::shutdown() {
    shader_.shutdown();
}

void tv_pass::draw(const unsigned int picture_texture, const picture_size& picture, const int window_width,
                   const int window_height, const glm::vec3& border) {
    framebuffer::bind_default();
    glViewport(0, 0, window_width, window_height);
    glClearColor(border.r, border.g, border.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Scanlines fade in with the rows per line: at one row there is no gap to draw.
    const float scanline_strength = std::clamp(static_cast<float>(picture.factor - 1) /
                                                   static_cast<float>(style_.scanline_full_factor - 1),
                                               0.0f, 1.0f);
    const float window_height_f = static_cast<float>(window_height);
    const float scene_height = static_cast<float>(std::max(1, picture.height));
    shader_.use();
    shader_.set_vec2("u_resolution", glm::vec2(static_cast<float>(window_width), window_height_f));
    shader_.set_int("u_factor", picture.factor);
    shader_.set_vec2("u_crop", glm::vec2(static_cast<float>(picture.crop_x), static_cast<float>(picture.crop_y)));
    shader_.set_vec4("u_beam", glm::vec4(style_.horizontal_blur, style_.scanline_depth * scanline_strength,
                                         style_.scanline_gap, style_.mask));
    shader_.set_vec4("u_screen", glm::vec4(style_.curvature, style_.corner_radius * window_height_f, style_.vignette, 0.0f));
    shader_.set_vec4("u_halation", glm::vec4(style_.halation, style_.halation_threshold,
                                             style_.halation_radius * scene_height, 0.0f));
    shader_.set_vec3("u_border", border);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, picture_texture);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(shader_.profile());
    glBindVertexArray(0);
}
