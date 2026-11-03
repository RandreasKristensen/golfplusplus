#include "renderer/sun_shaft_pass.h"

#include "renderer/gl_loader.h"
#include "renderer/scattered_light.h"

#include <SDL.h>

#include <filesystem>

#include <glm/matrix.hpp>

namespace {
constexpr GLsizei screen_quad_vertices = 6;
constexpr int depth_unit = 0;
constexpr int shafts_unit = 1;

std::string shader_file(const std::string& dir, const char* name) {
    return (std::filesystem::path(dir) / name).string();
}
}

bool sun_shaft_pass::init(const std::string& shader_dir, const unsigned int screen_vao, const shaft_style& style,
                          const int shadow_map_unit, const int lamp_map_unit) {
    screen_vao_ = screen_vao;
    style_ = style;
    shadow_map_unit_ = shadow_map_unit;
    lamp_map_unit_ = lamp_map_unit;
    const std::string vertex = shader_file(shader_dir, "crt.vert");
    const std::string steps = "#define SHAFT_STEPS " + std::to_string(style.steps) + "\n";
    const std::size_t lamp_variants = static_cast<std::size_t>(style.lamps) + 1;
    marches_.assign(2 * lamp_variants, shader_program{});
    for (const bool sun : {false, true}) {
        for (std::size_t lamps = 0; lamps < lamp_variants; ++lamps) {
            if (!sun && lamps == 0) {
                continue;
            }
            std::string defines = steps;
            if (sun) {
                defines += "#define SUN_SHAFTS\n";
            }
            if (lamps > 0) {
                defines += "#define LAMP_SHADOWS " + std::to_string(lamps) + "\n#define LAMP_TILES " +
                    std::to_string(style.lamps) + "\n";
            }
            shader_program& program = march(sun, lamps);
            if (!program.load_from_files(vertex, shader_file(shader_dir, "sun_shafts.frag"), defines)) {
                return false;
            }
            program.use();
            program.set_int("u_depth", depth_unit);
            program.set_int("u_shadow_map", shadow_map_unit_);
            program.set_int("u_lamp_shadow_map", lamp_map_unit_);
            program.set_float("u_reach", style_.reach_meters);
        }
    }
    if (!composite_.load_from_files(vertex, shader_file(shader_dir, "sun_shafts_composite.frag"))) {
        return false;
    }
    composite_.use();
    composite_.set_int("u_depth", depth_unit);
    composite_.set_int("u_shafts", shafts_unit);
    composite_.set_float("u_reach", style_.reach_meters);
    return true;
}

void sun_shaft_pass::shutdown() {
    destroy_targets();
    for (shader_program& program : marches_) {
        program.shutdown();
    }
    marches_.clear();
    composite_.shutdown();
}

shader_program& sun_shaft_pass::march(const bool sun, const std::size_t lamps) {
    return marches_[(sun ? marches_.size() / 2 : 0) + lamps];
}

void sun_shaft_pass::destroy_targets() {
    shafts_.shutdown();
    if (scene_fbo_ != 0) {
        glDeleteFramebuffers(1, &scene_fbo_);
        scene_fbo_ = 0;
    }
    scene_color_ = 0;
    width_ = 0;
    height_ = 0;
}

void sun_shaft_pass::set_profile(frame_profile* profile) {
    for (shader_program& program : marches_) {
        program.set_profile(profile);
    }
    composite_.set_profile(profile);
}

bool sun_shaft_pass::ensure_size(const int width, const int height, const unsigned int scene_color) {
    if (width == width_ && height == height_ && scene_color == scene_color_) {
        return true;
    }
    destroy_targets();
    if (!shafts_.init((width + 1) / 2, (height + 1) / 2,
                      framebuffer_format{framebuffer_color::signed_hdr, framebuffer_depth::none, false, false})) {
        return false;
    }
    glGenFramebuffers(1, &scene_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_color, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        SDL_Log("The sun shafts' way into the scene is incomplete.");
        destroy_targets();
        return false;
    }
    scene_color_ = scene_color;
    width_ = width;
    height_ = height;
    return true;
}

void sun_shaft_pass::draw(const frame_lighting_block& lighting, const shadow_map_pass& shadows, const lamp_shadow_pass& lamps,
                          const unsigned int scene_depth, const glm::mat4& view_proj, gl_timer_pool& timers) {
    const bool sun = shadows.strength() * shaft_peak(lighting, style_.reach_meters) >= style_.min_glow;
    const std::size_t lamp_count = lighting.scatter.x > 0.0f || lighting.mist.z > 0.0f ? lamps.count() : 0;
    if (scene_fbo_ == 0 || (!sun && lamp_count == 0)) {
        return;
    }
    timers.begin(gpu_profile_stage::shafts);
    const glm::mat4 inverse_view_proj = glm::inverse(view_proj);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(screen_vao_);
    glActiveTexture(GL_TEXTURE0 + depth_unit);
    glBindTexture(GL_TEXTURE_2D, scene_depth);

    shafts_.bind();
    glViewport(0, 0, shafts_.width(), shafts_.height());
    const shader_program& stepping = march(sun, lamp_count);
    stepping.use();
    stepping.set_mat4("u_inverse_view_proj", inverse_view_proj);
    if (sun) {
        shadows.bind(stepping, shadow_map_unit_);
    }
    if (lamp_count > 0) {
        lamps.bind(stepping, lamp_map_unit_);
    }
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(stepping.profile());

    // The scene minus the shafts' glow.
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glViewport(0, 0, width_, height_);
    composite_.use();
    composite_.set_mat4("u_inverse_view_proj", inverse_view_proj);
    glActiveTexture(GL_TEXTURE0 + shafts_unit);
    glBindTexture(GL_TEXTURE_2D, shafts_.color_texture());
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glBlendFunc(GL_ONE, GL_ONE);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(composite_.profile());
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    timers.end();
}
