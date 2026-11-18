#include "renderer/reflection_pass.h"
#include "renderer/gl_loader.h"
#include <algorithm>

namespace {
constexpr int reflection_resolution_divisor = 4;
}

bool reflection_pass::draw(const render_water* water, const glm::mat4& view, const glm::mat4& projection,
                            const glm::vec3& eye, gl_timer_pool& timers, const draw_scene& draw_scene_frame, const std::vector<float>* visible) {
    surfaces_.clear();
    if (!water || water->triangles.empty()) { return true; }
    GLint bound = 0, viewport[4] = {}, winding = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_FRONT_FACE, &winding);
    const GLboolean scissored = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    const auto levels = visible ? *visible:visible_water_levels(*water, projection * view);
    bool ok = true;
    timers.begin(gpu_profile_stage::reflection);
    for (const float level : levels) {
        if (eye.y <= level) { continue; } // Snell's window uses the scene above directly.
        const std::size_t i = surfaces_.size();
        if (i == targets_.size()) { targets_.emplace_back(); }
        framebuffer& target = targets_[i];
        // Geometry and lighting stay identical at this sampling resolution.
        const int width=std::max(1,viewport[2]/reflection_resolution_divisor);
        const int height=std::max(1,viewport[3]/reflection_resolution_divisor);
        if ((target.width() != width || target.height() != height) &&
            !target.init(width, height, {framebuffer_color::hdr, framebuffer_depth::drawn, true, false})) {
            ok = false; break;
        }
        const auto reflected = reflect_water_view(view, projection, eye, level);
        target.bind();
        glViewport(0, 0, target.width(), target.height());
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glFrontFace(winding == GL_CCW ? GL_CW : GL_CCW);
        draw_scene_frame(reflected);
        surfaces_.push_back({level, target.color_texture(), reflected.projection * reflected.view});
    }
    glFrontFace(static_cast<GLenum>(winding));
    if (scissored) { glEnable(GL_SCISSOR_TEST); }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    timers.end();
    return ok;
}

void reflection_pass::shutdown() {
    for (auto& target : targets_) { target.shutdown(); }
    targets_.clear();
    surfaces_.clear();
}
