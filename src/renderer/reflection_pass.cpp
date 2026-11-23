#include "renderer/reflection_pass.h"
#include "renderer/gl_loader.h"
#include <algorithm>

namespace {
constexpr int reflection_resolution_divisor = 4;
}

bool reflection_pass::draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye,
                           const draw_target& parent, const std::vector<water_candidate>& candidates, const draw_scene& draw_scene_frame,
                           frame_profile* profile) {
    surfaces_.clear();
    if (candidates.empty()) {
        trim_pool();
        return true;
    }
    glDisable(GL_SCISSOR_TEST);
    bool ok = true;
    if (profile) { profile->work.reflection_levels += candidates.size(); }
    for (const water_candidate& candidate : candidates) {
        const float level = candidate.level;
        if (eye.y <= level) { continue; } // Snell's window uses the scene above directly.
        const std::size_t i = surfaces_.size();
        if (i == targets_.size()) { targets_.emplace_back(storage_); }
        framebuffer& target = targets_[i];
        // The full quarter-scale picture fixes the sampling density and the
        // target's size; only the rectangle the water looks up is drawn, at the
        // target's first texels.
        const int width=std::max(1,parent.width/reflection_resolution_divisor);
        const int height=std::max(1,parent.height/reflection_resolution_divisor);
        if ((target.width() != width || target.height() != height) &&
            !target.init(width, height, {framebuffer_color::hdr, framebuffer_depth::drawn, true, false})) {
            ok = false; break;
        }
        const reflection_crop crop = snap_reflection_crop(candidate.coverage, width, height);
        auto reflected = reflect_water_view(view, projection, eye, level, reflection_crop_matrix(crop));
        reflected.crop = crop;
        reflected.target = draw_target{target.id(), crop.width, crop.height, false};
        target.bind();
        glViewport(0, 0, crop.width, crop.height);
        if (candidate.query != 0) { glBeginConditionalRender(candidate.query, GL_QUERY_NO_WAIT); }
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glFrontFace(GL_CW);
        if (profile) {
            ++profile->work.reflection_captures;
            profile->work.reflection_pixels += static_cast<std::uint64_t>(crop.width) * crop.height;
        }
        draw_scene_frame(reflected);
        if (candidate.query != 0) { glEndConditionalRender(); }
        surfaces_.push_back({level, target.color_texture(),
                             reflection_texture_matrix(crop) * reflected.projection * reflected.view,
                             reflection_lookup_range(crop)});
    }
    trim_pool();
    glFrontFace(GL_CCW);
    if (parent.scissored) { glEnable(GL_SCISSOR_TEST); }
    glBindFramebuffer(GL_FRAMEBUFFER, parent.framebuffer);
    glViewport(0, 0, parent.width, parent.height);
    return ok;
}

void reflection_pass::trim_pool() {
    if (targets_.size() <= surfaces_.size()) {
        idle_frames_ = 0;
        return;
    }
    if (++idle_frames_ < pool_idle_frames) { return; }
    for (std::size_t i = surfaces_.size(); i < targets_.size(); ++i) { targets_[i].shutdown(); }
    targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(surfaces_.size()), targets_.end());
    idle_frames_ = 0;
}

void reflection_pass::shutdown() {
    for (auto& target : targets_) { target.shutdown(); }
    targets_.clear();
    surfaces_.clear();
}
