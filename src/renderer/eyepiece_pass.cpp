#include "renderer/eyepiece_pass.h"

#include "renderer/gl_loader.h"
#include "renderer/rangefinder_overlay.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr GLsizei screen_quad_vertices = 6;
constexpr int scene_unit = 0;
constexpr int zoom_unit = 1;

// The round view's radius in pixels of a target `height` tall.
float view_radius(const int height) {
    return eyepiece_radius_share * static_cast<float>(height) * 0.5f;
}
}

glm::mat4 zoomed_projection(const glm::mat4& proj, const float magnification) {
    glm::mat4 zoomed = proj;
    const float scale = std::max(1.0f, magnification);
    zoomed[0][0] *= scale;
    zoomed[1][1] *= scale;
    return zoomed;
}

bool eyepiece_pass::init(const std::string& vertex_path, const std::string& fragment_path, const unsigned int screen_vao) {
    screen_vao_ = screen_vao;
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    shader_.use();
    shader_.set_int("u_scene", scene_unit);
    shader_.set_int("u_zoom", zoom_unit);
    return true;
}

void eyepiece_pass::shutdown() {
    shader_.shutdown();
    target_.shutdown();
    zoom_.shutdown();
}

bool eyepiece_pass::ensure_size(const int width, const int height) {
    if (target_.width() == width && target_.height() == height) {
        return true;
    }
    return target_.init(width, height, framebuffer_format{framebuffer_color::hdr, false, false, false}) &&
        zoom_.init(width, height, framebuffer_format{framebuffer_color::hdr, true, false, false});
}

void eyepiece_pass::begin_zoom(const glm::vec3& clear) {
    zoom_.bind();
    glViewport(0, 0, zoom_.width(), zoom_.height());
    const int radius = static_cast<int>(std::ceil(view_radius(zoom_.height()))) + 1;
    glEnable(GL_SCISSOR_TEST);
    glScissor(zoom_.width() / 2 - radius, zoom_.height() / 2 - radius, 2 * radius, 2 * radius);
    glClearColor(clear.r, clear.g, clear.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void eyepiece_pass::end_zoom() {
    glDisable(GL_SCISSOR_TEST);
}

unsigned int eyepiece_pass::draw(const unsigned int scene_texture, const render_eyepiece& eyepiece, gl_timer_pool& timers) {
    timers.begin(gpu_profile_stage::eyepiece);
    target_.bind();
    glViewport(0, 0, target_.width(), target_.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    shader_.use();
    shader_.set_vec3("u_eyepiece", glm::vec3(view_radius(target_.height()), eyepiece.surround_brightness,
                                             eyepiece.surround_saturation));
    glActiveTexture(GL_TEXTURE0 + zoom_unit);
    glBindTexture(GL_TEXTURE_2D, zoom_.color_texture());
    glActiveTexture(GL_TEXTURE0 + scene_unit);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(shader_.profile());
    glBindVertexArray(0);
    timers.end();
    return target_.color_texture();
}
