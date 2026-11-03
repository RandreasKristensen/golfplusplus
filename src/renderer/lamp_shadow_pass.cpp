#include "renderer/lamp_shadow_pass.h"

#include "renderer/gl_loader.h"

#include <SDL.h>

#include <algorithm>

#include <glm/vec4.hpp>

namespace {
// Air this close behind a caster seen from the lamp still counts as lit.
constexpr float depth_bias_meters = 0.05f;
}

bool lamp_shadow_pass::init(const std::string& vertex_path, const std::string& fragment_path, const shaft_style& style) {
    shutdown();
    style_ = style;
    if (style.lamps <= 0) {
        return true;
    }
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenTextures(1, &depth_texture_);
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, style.lamps * style.lamp_map_resolution, style.lamp_map_resolution,
                 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        SDL_Log("The floodlights' shadow maps' framebuffer is incomplete.");
        shutdown();
        return false;
    }
    held_.assign(static_cast<std::size_t>(style.lamps), std::nullopt);
    return true;
}

void lamp_shadow_pass::shutdown() {
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (depth_texture_ != 0) {
        glDeleteTextures(1, &depth_texture_);
        depth_texture_ = 0;
    }
    shader_.shutdown();
    held_.clear();
    slots_.clear();
    view_projs_.clear();
    casters_known_ = false;
}

void lamp_shadow_pass::update(const std::vector<render_light>& lights, const glm::vec3& eye, tree_renderer& tree_batch,
                              const std::vector<tree_body>& trees, const std::uint64_t trees_revision,
                              floodlight_renderer& floodlight_batch, const render_floodlights* floodlights,
                              gl_timer_pool& timers, frame_profile* profile) {
    slots_.clear();
    view_projs_.clear();
    const bool any_casters = !trees.empty() || (floodlights != nullptr && !floodlights->vertices.empty());
    if (fbo_ == 0 || !any_casters) {
        return;
    }
    const std::uint64_t floodlights_revision = floodlights != nullptr ? floodlights->revision : 0;
    if (!casters_known_ || trees_revision != trees_revision_ || floodlights_revision != floodlights_revision_) {
        std::fill(held_.begin(), held_.end(), std::nullopt);
        trees_revision_ = trees_revision;
        floodlights_revision_ = floodlights_revision;
        casters_known_ = true;
    }
    const lamp_tiling tiling =
        place_lamps(held_, lights, shadowed_lamps(lights, eye, style_.reach_meters, held_.size()));
    held_ = tiling.held;
    slots_ = tiling.slots;
    for (const lamp_slot& slot : slots_) {
        view_projs_.push_back(lamp_shadow_view_proj(*held_[slot.tile], style_.lamp_near_meters));
    }
    if (tiling.redraw.empty()) {
        return;
    }
    timers.begin(gpu_profile_stage::lamp_shadows);
    const int resolution = style_.lamp_map_resolution;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_SCISSOR_TEST);
    shader_.set_profile(profile);
    shader_.use();
    for (const std::size_t tile : tiling.redraw) {
        const int left = static_cast<int>(tile) * resolution;
        glViewport(left, 0, resolution, resolution);
        glScissor(left, 0, resolution, resolution);
        glClear(GL_DEPTH_BUFFER_BIT);
        const glm::mat4 view_proj = lamp_shadow_view_proj(*held_[tile], style_.lamp_near_meters);
        tree_batch.draw_casters(trees, trees_revision, shader_, view_proj, profile);
        floodlight_batch.draw_casters(floodlights, shader_, view_proj, profile);
    }
    glDisable(GL_SCISSOR_TEST);
    timers.end();
}

void lamp_shadow_pass::bind(const shader_program& receiver, const int unit) const {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    glActiveTexture(GL_TEXTURE0);
    const float closest = style_.lamp_near_meters;
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        const lamp_view& lamp = *held_[slots_[s].tile];
        const float furthest = std::max(lamp.radius, closest * 2.0f);
        const std::string index = "[" + std::to_string(s) + "]";
        receiver.set_mat4(("u_lamp_view_proj" + index).c_str(), view_projs_[s]);
        // The bias in window depth at w metres out along the beam is this over w².
        receiver.set_vec4(("u_lamp" + index).c_str(),
                          glm::vec4(static_cast<float>(slots_[s].light), static_cast<float>(slots_[s].tile),
                                    depth_bias_meters * furthest * closest / (furthest - closest), 0.0f));
    }
}
