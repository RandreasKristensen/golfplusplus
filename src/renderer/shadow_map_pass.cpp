#include "renderer/shadow_map_pass.h"

#include "renderer/gl_loader.h"
#include "renderer/shadow_map_fit.h"

#include <SDL.h>

#include <cstddef>

#include <glm/vec4.hpp>

namespace {
// Ground this close behind a caster along the sun still counts as lit, so a
// trunk's shadow starts at its foot without speckling it.
constexpr float depth_bias_meters = 0.05f;
// shadow_map.vert's inputs: the figures' positions, drawn as one unscaled
// instance at the origin.
constexpr GLuint position_location = 0;
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;
}

bool shadow_map_pass::init(const std::string& vertex_path, const std::string& fragment_path, const shadow_style& style) {
    shutdown();
    style_ = style;
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenTextures(1, &depth_texture_);
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, style.map_resolution, style.map_resolution, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    // Linear with a comparison: each tap is already a 2x2 soft test.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &figures_vao_);
    glBindVertexArray(figures_vao_);
    // init() leaves the buffer bound for the attribute pointer below.
    if (figures_vao_ == 0 || !figures_buffer_.init(0)) {
        SDL_Log("The shadow map failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, position)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        SDL_Log("The shadow map's framebuffer is incomplete.");
        shutdown();
    }
    return complete;
}

void shadow_map_pass::shutdown() {
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (depth_texture_ != 0) {
        glDeleteTextures(1, &depth_texture_);
        depth_texture_ = 0;
    }
    figures_buffer_.shutdown();
    if (figures_vao_ != 0) {
        glDeleteVertexArrays(1, &figures_vao_);
        figures_vao_ = 0;
    }
    shader_.shutdown();
    strength_ = 0.0f;
}

void shadow_map_pass::draw(tree_renderer& tree_batch, const std::vector<tree_body>& trees, const std::uint64_t trees_revision,
                           floodlight_renderer& floodlight_batch, const render_floodlights* floodlights,
                           const world_marker_batch& figures, const glm::vec3& towards_sun, const float sun_share, const glm::vec3& eye,
                           const glm::vec3& forward, gl_timer_pool& timers, frame_profile* profile) {
    strength_ = style_.map_strength * sun_share;
    if (fbo_ == 0 || strength_ <= 0.0f || towards_sun.y <= 0.0f ||
        (trees.empty() && (floodlights == nullptr || floodlights->vertices.empty()) && figures.empty())) {
        strength_ = 0.0f;
        return;
    }
    const shadow_map_fit fit =
        fit_shadow_map(towards_sun, eye, forward, style_.map_extent, style_.map_resolution, style_.map_ahead);
    view_proj_ = fit.view_proj;
    depth_bias_ = depth_bias_meters / fit.depth_range;

    timers.begin(gpu_profile_stage::shadow_map);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, style_.map_resolution, style_.map_resolution);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    shader_.set_profile(profile);
    shader_.use();
    tree_batch.draw_casters(trees, trees_revision, shader_, view_proj_, profile);
    floodlight_batch.draw_casters(floodlights, shader_, view_proj_, profile);
    draw_figures(figures, profile);
    timers.end();
}

void shadow_map_pass::draw_figures(const world_marker_batch& figures, frame_profile* profile) {
    if (figures.empty()) {
        return;
    }
    figures_buffer_.upload(figures.vertices().data(), figures.vertices().size() * sizeof(world_marker_vertex), profile);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    shader_.set_mat4("u_view_proj", view_proj_);
    glBindVertexArray(figures_vao_);
    glVertexAttrib3f(instance_offset_location, 0.0f, 0.0f, 0.0f);
    glVertexAttrib3f(instance_scale_location, 1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(figures.vertices().size()));
    record_draw_call(profile);
    glBindVertexArray(0);
}

void shadow_map_pass::bind(const shader_program& receiver, const int unit) const {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    glActiveTexture(GL_TEXTURE0);
    receiver.set_mat4("u_shadow_view_proj", view_proj_);
    receiver.set_vec4("u_shadow", glm::vec4(strength_, style_.map_softness / static_cast<float>(style_.map_resolution),
                                           depth_bias_, style_.map_edge_fade));
}
