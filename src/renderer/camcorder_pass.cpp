#include "renderer/camcorder_pass.h"

#include "renderer/gl_loader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr GLsizei screen_quad_vertices = 6;
constexpr int quarter = 4;
// Longer gaps (a hitch, a still frame) ease no further than this.
constexpr float max_lag_step_seconds = 0.25f;
// The bloom shader's taps either side of the centre, sideways and up and down.
constexpr float bloom_taps_sideways = 4.0f;
constexpr float bloom_taps_vertical = 1.0f;
constexpr int adapt_unit = 3;

std::string shader_file(const std::string& dir, const char* name) {
    return (std::filesystem::path(dir) / name).string();
}

// How far a lag of `seconds` eases in `dt`.
float ease_share(const float dt, const float seconds) {
    return 1.0f - std::exp(-dt / seconds);
}
}

bool camcorder_pass::init(const std::string& shader_dir, const unsigned int screen_vao, const camcorder_style& style,
                          const float fields_per_second) {
    screen_vao_ = screen_vao;
    style_ = style;
    fields_per_second_ = fields_per_second;
    const std::string vertex = shader_file(shader_dir, "crt.vert");
    const bool ok = downsample_.load_from_files(vertex, shader_file(shader_dir, "camcorder_downsample.frag")) &&
        adapt_.load_from_files(vertex, shader_file(shader_dir, "camcorder_adapt.frag")) &&
        bloom_.load_from_files(vertex, shader_file(shader_dir, "camcorder_bloom.frag")) &&
        smear_.load_from_files(vertex, shader_file(shader_dir, "camcorder_smear.frag")) &&
        record_.load_from_files(vertex, shader_file(shader_dir, "camcorder.frag"));
    if (!ok) {
        return false;
    }
    downsample_.use();
    downsample_.set_int("u_scene", 0);
    adapt_.use();
    adapt_.set_int("u_quarter", 0);
    adapt_.set_int("u_previous", 1);
    bloom_.use();
    bloom_.set_int("u_quarter", 0);
    bloom_.set_int("u_adapt", adapt_unit);
    smear_.use();
    smear_.set_int("u_scene", 0);
    smear_.set_int("u_adapt", adapt_unit);
    smear_.set_float("u_threshold", style_.smear_threshold);
    record_.use();
    record_.set_int("u_scene", 0);
    record_.set_int("u_bloom", 1);
    record_.set_int("u_smear", 2);
    record_.set_int("u_adapt", adapt_unit);
    record_.set_vec3("u_shadow_tint", style_.shadow_tint);
    record_.set_float("u_smear_fade", style_.smear_fade);
    return true;
}

void camcorder_pass::shutdown() {
    for (shader_program* shader : {&downsample_, &adapt_, &bloom_, &smear_, &record_}) {
        shader->shutdown();
    }
    for (framebuffer* target : {&quarter_, &bloom_target_, &smear_target_, &adapt_targets_[0], &adapt_targets_[1], &picture_}) {
        target->shutdown();
    }
    has_frame_ = false;
}

void camcorder_pass::set_profile(frame_profile* profile) {
    for (shader_program* shader : {&downsample_, &adapt_, &bloom_, &smear_, &record_}) {
        shader->set_profile(profile);
    }
}

bool camcorder_pass::ensure_size(const int width, const int height) {
    if (picture_.width() == width && picture_.height() == height) {
        return true;
    }
    has_frame_ = false;
    const int quarter_width = std::max(1, (width + quarter - 1) / quarter);
    const int quarter_height = std::max(1, (height + quarter - 1) / quarter);
    const framebuffer_format light{framebuffer_color::hdr, false, true, false};
    const framebuffer_format metered{framebuffer_color::hdr, false, true, true};
    const framebuffer_format lag{framebuffer_color::signed_hdr, false, false, false};
    // Its alpha is where down the picture each column's light is.
    const framebuffer_format column = lag;
    return quarter_.init(quarter_width, quarter_height, metered) && bloom_target_.init(quarter_width, quarter_height, light) &&
        smear_target_.init(width, 1, column) && adapt_targets_[0].init(1, 1, lag) && adapt_targets_[1].init(1, 1, lag) &&
        picture_.init(width, height, framebuffer_format{framebuffer_color::rgb8, false, false, false});
}

void camcorder_pass::draw_quad(const shader_program& shader) {
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(shader.profile());
}

void camcorder_pass::set_iris(const shader_program& shader, const float exposure) const {
    shader.set_vec4("u_iris", glm::vec4(style_.middle_grey, std::exp2(style_.iris_min_stops), std::exp2(style_.iris_max_stops),
                                        exposure));
}

void camcorder_pass::draw(const unsigned int scene_texture, const render_lighting& light, const float seconds,
                          gl_timer_pool& timers) {
    const float dt = std::clamp(seconds - last_seconds_, 0.0f, max_lag_step_seconds);
    last_seconds_ = seconds;
    // A fresh camera meters this frame as it is.
    const glm::vec2 blend = has_frame_ ? glm::vec2(ease_share(dt, style_.iris_seconds), ease_share(dt, style_.white_balance_seconds))
                                       : glm::vec2(1.0f);
    const int previous = adapt_index_;
    adapt_index_ = 1 - adapt_index_;
    const framebuffer& adapt = adapt_targets_[static_cast<std::size_t>(adapt_index_)];
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    timers.begin(gpu_profile_stage::bloom);
    quarter_.bind();
    glViewport(0, 0, quarter_.width(), quarter_.height());
    downsample_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    draw_quad(downsample_);
    glBindTexture(GL_TEXTURE_2D, quarter_.color_texture());
    glGenerateMipmap(GL_TEXTURE_2D);

    adapt.bind();
    glViewport(0, 0, 1, 1);
    adapt_.use();
    adapt_.set_vec3("u_light", light.ambient_sky + light.sun_color);
    adapt_.set_vec2("u_blend", blend);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, adapt_targets_[static_cast<std::size_t>(previous)].color_texture());
    draw_quad(adapt_);
    glActiveTexture(GL_TEXTURE0 + adapt_unit);
    glBindTexture(GL_TEXTURE_2D, adapt.color_texture());

    bloom_target_.bind();
    glViewport(0, 0, bloom_target_.width(), bloom_target_.height());
    bloom_.use();
    set_iris(bloom_, light.exposure);
    const float aspect = static_cast<float>(picture_.height()) / static_cast<float>(std::max(1, picture_.width()));
    bloom_.set_vec3("u_bloom", glm::vec3(style_.bloom_threshold, style_.bloom_width * aspect / bloom_taps_sideways,
                                         style_.bloom_height / bloom_taps_vertical));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, quarter_.color_texture());
    draw_quad(bloom_);
    timers.end();

    timers.begin(gpu_profile_stage::smear);
    smear_target_.bind();
    glViewport(0, 0, smear_target_.width(), 1);
    smear_.use();
    set_iris(smear_, light.exposure);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    draw_quad(smear_);
    timers.end();

    timers.begin(gpu_profile_stage::camcorder);
    picture_.bind();
    glViewport(0, 0, picture_.width(), picture_.height());
    record_.use();
    set_iris(record_, light.exposure);
    record_.set_vec4("u_white_balance", glm::vec4(light.white_balance, style_.white_balance_correction));
    record_.set_vec4("u_tone", glm::vec4(style_.shoulder, style_.toe, light.saturation, style_.bloom_strength));
    record_.set_vec4("u_gain", glm::vec4(light.gain_noise, style_.gain_grain, style_.gain_chroma, style_.smear_strength));
    record_.set_float("u_field", std::floor(seconds * fields_per_second_));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom_target_.color_texture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, smear_target_.color_texture());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    draw_quad(record_);
    glBindVertexArray(0);
    timers.end();
    has_frame_ = true;
}
