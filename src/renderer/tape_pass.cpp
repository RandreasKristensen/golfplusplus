#include "renderer/tape_pass.h"

#include "renderer/gl_loader.h"

#include <algorithm>

#include <glm/vec4.hpp>

namespace {
constexpr GLsizei screen_quad_vertices = 6;
// A Gaussian of sigma s halves a wave of f cycles per pixel where s f is this.
constexpr float gaussian_half_amplitude = 0.18739f;
// Dropouts last a whole frame, two fields.
constexpr float fields_per_frame = 2.0f;

// The sigma, in pixels of a picture `height` tall, that halves detail at
// `lines` lines (lines / 2 cycles) per picture height.
float blur_sigma(const float lines, const float height) {
    return gaussian_half_amplitude * 2.0f * height / lines;
}
}

bool tape_pass::init(const std::string& vertex_path, const std::string& fragment_path, const unsigned int screen_vao,
                     const tape_style& style) {
    screen_vao_ = screen_vao;
    style_ = style;
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    shader_.use();
    shader_.set_int("u_source", 0);
    return true;
}

void tape_pass::shutdown() {
    shader_.shutdown();
    target_.shutdown();
}

bool tape_pass::ensure_size(const int width, const int height) {
    if (target_.width() == width && target_.height() == height) {
        return true;
    }
    return target_.init(width, height);
}

void tape_pass::draw(const unsigned int picture_texture, const float seconds, const float tracking) {
    const float height = static_cast<float>(std::max(1, target_.height()));
    const tape_look look = tape_look_at(style_, wear_);
    const float frames_per_second = style_.fields_per_second / fields_per_frame;

    target_.bind();
    glViewport(0, 0, target_.width(), target_.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    shader_.use();
    shader_.set_float("u_seconds", seconds);
    shader_.set_float("u_tracking", tracking);
    shader_.set_vec4("u_blur", glm::vec4(blur_sigma(look.luma_lines, height), blur_sigma(look.chroma_lines, height),
                                         look.chroma_delay * height, look.peaking));
    shader_.set_vec4("u_tone", glm::vec4(look.black_lift, look.highlight_knee, look.saturation, style_.fields_per_second));
    shader_.set_vec4("u_noise", glm::vec4(look.grain, style_.grain_width * height, look.chroma_noise,
                                          style_.chroma_noise_size * height));
    shader_.set_vec4("u_wobble", glm::vec4(look.wobble * height, style_.wobble_span * height,
                                           style_.head_switch_height * height, look.head_switch_skew * height));
    shader_.set_vec4("u_faults", glm::vec4(look.head_switch_noise, look.dropouts_per_second / (frames_per_second * height),
                                           style_.dropout_length * height, 0.0f));
    shader_.set_vec4("u_tracking_style", glm::vec4(style_.tracking_height * height, style_.tracking_speed * height,
                                                   style_.tracking_noise, style_.tracking_skew * height));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, picture_texture);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(shader_.profile());
    glBindVertexArray(0);
}
