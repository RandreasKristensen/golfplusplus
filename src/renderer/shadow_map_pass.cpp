#include "renderer/shadow_map_pass.h"

#include "renderer/gl_loader.h"
#include "renderer/shadow_map_fit.h"

#include <SDL.h>

#include <cmath>
#include <cstddef>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

namespace {
// Ground this close behind a caster along the sun still counts as lit, so a
// trunk's shadow starts at its foot without speckling it.
constexpr float depth_bias_meters = 0.05f;
// shadow_map.vert's inputs: the figures' triangles' positions, drawn as
// one unscaled instance at the origin.
constexpr GLuint position_location = 0;
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;
}

bool shadow_map_pass::init(const std::string& vertex_path, const std::string& fragment_path, const shadow_style& style) {
    shutdown();
    style_ = style;
    if (!shader_.load_from_files(vertex_path, fragment_path) ||
        !tree_shader_.load_from_files(vertex_path, fragment_path, "#define TREE_CASTER\n") ||
        !model_shader_.load_from_files(vertex_path, fragment_path, "#define MODEL_CASTER\n") || !meshes_.init()) {
        return false;
    }
    glGenTextures(1, &depth_texture_);
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    storage_.texture_image(depth_texture_, GL_DEPTH_COMPONENT24, style.map_resolution, style.map_resolution, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    // Linear with a comparison: each tap is already a 2x2 soft test.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &models_vao_);
    glGenVertexArrays(1, &static_vao_);
    glBindVertexArray(static_vao_);
    if (models_vao_ == 0 || static_vao_ == 0 || !static_buffer_.init(0)) {
        SDL_Log("The shadow map failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
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
    storage_.delete_texture(depth_texture_);
    static_buffer_.shutdown();
    static_held_ = false;
    drawn_ = drawn_map{};
    for (unsigned int* vao : {&models_vao_, &static_vao_}) {
        if (*vao != 0) {
            glDeleteVertexArrays(1, vao);
            *vao = 0;
        }
    }
    shader_.shutdown();
    tree_shader_.shutdown();
    model_shader_.shutdown();
    meshes_.shutdown();
    strength_ = 0.0f;
}

void shadow_map_pass::draw(tree_renderer& tree_batch, floodlight_renderer& floodlight_batch, const render_floodlights* floodlights,
                           const marker_caster_source& figures, const prop_caster_source& models, const glm::vec3& towards_sun,
                           const float sun_share, const glm::vec3& eye,
                           gl_timer_pool& timers, frame_profile* profile) {
    strength_ = style_.map_strength * sun_share;
    if (fbo_ == 0 || strength_ <= 0.0f || towards_sun.y <= 0.0f ||
        (tree_batch.tree_count() == 0 && (floodlights == nullptr || floodlights->vertices.empty()) && (figures.batch == nullptr || figures.batch->empty()) &&
         !has_instances(models) && static_index_.vertex_count() == 0)) {
        strength_ = 0.0f;
        return;
    }
    // The held map keeps its sun while the sun has turned too little to show, and while the camera has stayed
    // on the texel grid it was centred on.
    const bool sun_held = drawn_.valid && glm::length(towards_sun - drawn_.towards_sun) <= glm::radians(style_.map_sun_degrees);
    const shadow_map_fit fit = fit_shadow_map(towards_sun, eye, style_.map_extent, style_.map_resolution);
    const shadow_map_fit same_sun = fit_shadow_map(drawn_.towards_sun, eye, style_.map_extent, style_.map_resolution);
    const bool held = sun_held && same_sun.corner == drawn_.corner &&
        std::abs(same_sun.depth_middle - drawn_.depth_middle) <= style_.map_extent;
    const std::uint64_t floodlights_revision = floodlights != nullptr ? floodlights->revision : 0;
    const auto moving = [&](const auto& visit) { each_moving(figures, models, visit); };
    if (held && drawn_.trees == tree_batch.trees_revision() && drawn_.tree_models == tree_batch.model_revision() &&
        drawn_.floodlights == floodlights_revision && drawn_.static_casters == static_epoch_ &&
        drawn_.static_models == models.static_revision && drawn_.moving.within(moving, pose_tolerance_meters()) &&
        drawn_.pose.within(tree_batch.motion_time(), tree_batch.motion_wind(), tree_batch.pose_reach(), pose_tolerance_meters())) {
        return;
    }
    view_proj_ = fit.view_proj;
    depth_bias_ = depth_bias_meters / fit.depth_range;
    drawn_.valid = true;
    drawn_.towards_sun = towards_sun;
    drawn_.corner = fit.corner;
    drawn_.depth_middle = fit.depth_middle;
    drawn_.trees = tree_batch.trees_revision();
    drawn_.tree_models = tree_batch.model_revision();
    drawn_.floodlights = floodlights_revision;
    drawn_.static_casters = static_epoch_;
    drawn_.static_models = models.static_revision;
    drawn_.moving.hold(moving);
    drawn_.pose.remember(tree_batch.motion_time(), tree_batch.motion_wind());

    const profile_pass_scope pass(profile, profile_pass::sun_shadow);
    timers.begin(gpu_profile_stage::shadow_map);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, style_.map_resolution, style_.map_resolution);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    shader_.set_profile(profile);
    tree_shader_.set_profile(profile);
    shader_.use();
    tree_shader_.use();
    tree_batch.draw_casters(tree_shader_, view_proj_, profile);
    shader_.use();
    floodlight_batch.draw_casters(floodlights, shader_, view_proj_, profile);
    draw_figures(figures, profile);
    draw_instances(figures, models, profile);
    draw_static_casters(eye, profile);
    timers.end();
}

void shadow_map_pass::set_static_casters(const std::vector<glm::vec3>& corners, frame_profile* profile) {
    if (static_buffer_.id() == 0) {
        return;
    }
    const std::vector<glm::vec3> sorted = static_index_.build(corners);
    static_buffer_.upload(sorted.data(), sorted.size() * sizeof(glm::vec3), profile, upload_kind::cached);
    ++static_epoch_;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    static_held_ = true;
}

void shadow_map_pass::draw_vertices(const unsigned int buffer, const std::size_t first, const std::size_t count,
                                    frame_profile* profile) {
    if (buffer == 0 || count == 0) {
        return;
    }
    // The world marker batch's triangles: positions lead its vertex layout.
    glBindVertexArray(models_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, sizeof(world_marker_vertex), nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    shader_.set_mat4("u_view_proj", view_proj_);
    glVertexAttrib3f(instance_offset_location, 0.0f, 0.0f, 0.0f);
    glVertexAttrib3f(instance_scale_location, 1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first), static_cast<GLsizei>(count));
    record_draw_call(profile);
    glBindVertexArray(0);
}

void shadow_map_pass::draw_instances(const marker_caster_source& figures, const prop_caster_source& models, frame_profile* profile) {
    model_shader_.set_profile(profile);
    model_shader_.use();
    model_shader_.set_mat4("u_view_proj", view_proj_);
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        const std::size_t m = static_cast<std::size_t>(mesh);
        meshes_.draw(models.static_buffer, mesh, models.static_runs[m].first, models.static_runs[m].count, profile);
        if (models.frame_batch == nullptr) {
            continue;
        }
        const std::size_t base = models.frame_batch->joined_first(false, mesh);
        for (int i = 0; i < models.frame[m].count; ++i) {
            const prop_range& run = models.frame[m].run[static_cast<std::size_t>(i)];
            meshes_.draw(models.frame_buffer, mesh, base + run.first, run.count, profile);
        }
    }
    if (figures.batch != nullptr) {
        for (const world_marker_instance_run& run : figures.batch->instance_runs()) {
            if (figures.batch->shown_in(run.part, marker_view::sun_shadow)) {
                meshes_.draw(figures.instance_buffer, run.mesh, figures.batch->instance_base(run.mesh) + run.first, run.count, profile);
            }
        }
    }
    shader_.use();
}

void shadow_map_pass::draw_figures(const marker_caster_source& figures, frame_profile* profile) {
    if (figures.batch == nullptr) {
        return;
    }
    for (const world_marker_run& run : figures.batch->runs()) {
        if (figures.batch->shown_in(run, marker_view::sun_shadow)) {
            draw_vertices(figures.buffer, run.first, run.count, profile);
        }
    }
}

void shadow_map_pass::draw_static_casters(const glm::vec3& eye, frame_profile* profile) {
    if (!static_held_) {
        return;
    }
    const std::vector<caster_run>& runs = static_index_.select(eye, style_.map_extent);
    if (runs.empty()) {
        return;
    }
    shader_.set_mat4("u_view_proj", view_proj_);
    glBindVertexArray(static_vao_);
    glVertexAttrib3f(instance_offset_location, 0.0f, 0.0f, 0.0f);
    glVertexAttrib3f(instance_scale_location, 1.0f, 1.0f, 1.0f);
    for (const caster_run& run : runs) {
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first), static_cast<GLsizei>(run.count));
        record_draw_call(profile);
    }
    glBindVertexArray(0);
}

bool shadow_map_pass::has_instances(const prop_caster_source& models) {
    for (std::size_t m = 0; m < prop_mesh_count; ++m) {
        if (models.static_runs[m].count > 0 || models.frame[m].count > 0) { return true; }
    }
    return false;
}

void shadow_map_pass::bind(const shader_program& receiver, const int unit) const {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    glActiveTexture(GL_TEXTURE0);
    receiver.set_int("u_shadow_map", unit);
    receiver.set_mat4("u_shadow_view_proj", view_proj_);
    receiver.set_vec4("u_shadow", glm::vec4(strength_, style_.map_softness / static_cast<float>(style_.map_resolution),
                                           depth_bias_, style_.map_edge_fade));
}
