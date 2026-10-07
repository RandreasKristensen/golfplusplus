#include "renderer/water_renderer.h"

#include <SDL.h>

#include "renderer/gl_loader.h"
#include "renderer/terrain_palette.h"

namespace {
constexpr GLuint position_location = 0;
// How much of what is under a pond's surface shows through it.
constexpr float surface_alpha = 0.62f;
// The scene from under the water: murky blue, most of the way.
const glm::vec3 underwater_color(0.06f, 0.18f, 0.32f);
constexpr float underwater_alpha = 0.68f;
}

bool water_renderer::init(const std::string& surface_vertex, const std::string& surface_fragment,
                          const std::string& tint_vertex, const std::string& tint_fragment, const unsigned int screen_vao) {
    shutdown();
    screen_vao_ = screen_vao;
    if (!surface_shader_.load_from_files(surface_vertex, surface_fragment) ||
        !tint_shader_.load_from_files(tint_vertex, tint_fragment)) {
        shutdown();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        SDL_Log("Water renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointer below.
    if (!buffer_.init(0)) {
        SDL_Log("Water renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void water_renderer::shutdown() {
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    surface_shader_.shutdown();
    tint_shader_.shutdown();
    screen_vao_ = 0;
    uploaded_ = false;
}

void water_renderer::draw_surfaces(const render_water* water, const glm::mat4& view_proj, const scene_haze& haze,
                                   frame_profile* profile) {
    if (water == nullptr || water->triangles.empty() || surface_shader_.id() == 0 || vao_ == 0) {
        return;
    }
    if (!uploaded_ || uploaded_revision_ != water->revision) {
        buffer_.upload(water->triangles.data(), water->triangles.size() * sizeof(glm::vec3), profile);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        vertex_count_ = water->triangles.size();
        uploaded_revision_ = water->revision;
        uploaded_ = true;
    }
    surface_shader_.set_profile(profile);
    surface_shader_.use();
    surface_shader_.set_mat4("u_view_proj", view_proj);
    surface_shader_.set_vec3("u_color", terrain_material_color(terrain_material::water));
    surface_shader_.set_float("u_alpha", surface_alpha);
    set_scene_haze_uniforms(surface_shader_, haze);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_count_));
    record_draw_call(profile);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void water_renderer::draw_underwater(frame_profile* profile) {
    if (tint_shader_.id() == 0 || screen_vao_ == 0) {
        return;
    }
    tint_shader_.set_profile(profile);
    tint_shader_.use();
    tint_shader_.set_vec3("u_color", underwater_color);
    tint_shader_.set_float("u_alpha", underwater_alpha);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    record_draw_call(profile);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
