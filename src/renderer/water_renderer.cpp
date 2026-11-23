#include "renderer/water_renderer.h"

#include <SDL.h>

#include "renderer/gl_loader.h"
#include "renderer/render_data.h"
#include "renderer/water_light.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <vector>
#include <string>

#include <glm/geometric.hpp>

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint fetch_low_location = 1;
constexpr GLuint fetch_high_location = 2;
constexpr int beneath_color_unit = 6;
constexpr int beneath_depth_unit = 7;
constexpr int reflection_unit = 9;
}

bool water_renderer::init(const std::string& surface_vertex, const std::string& surface_fragment,
                          const std::string& underwater_vertex, const std::string& underwater_fragment,
                          const unsigned int screen_vao) {
    shutdown();
    screen_vao_ = screen_vao;
    const auto shaders=std::filesystem::path(surface_vertex).parent_path();
    if (!visibility_shader_.load_from_files((shaders/"water_visibility.vert").string(),(shaders/"shadow_map.frag").string()) ||
        !surface_shader_.load_from_files(surface_vertex, surface_fragment) ||
        !underwater_shader_.load_from_files(underwater_vertex, underwater_fragment)) {
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
    static_assert(water_fetch_directions == 8, "the fetch goes up in two vec4 attributes");
    constexpr GLsizei stride = sizeof(render_water_vertex);
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(render_water_vertex, position)));
    glEnableVertexAttribArray(fetch_low_location);
    glVertexAttribPointer(fetch_low_location, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(render_water_vertex, fetch)));
    glEnableVertexAttribArray(fetch_high_location);
    glVertexAttribPointer(fetch_high_location, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(render_water_vertex, fetch) + 4 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void water_renderer::shutdown() {
    if(!visibility_queries_.empty()) {glDeleteQueries(static_cast<GLsizei>(visibility_queries_.size()),visibility_queries_.data());}
    visibility_queries_.clear();
    visibility_shader_.shutdown();
    beneath_.shutdown();
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    surface_shader_.shutdown();
    underwater_shader_.shutdown();
    screen_vao_ = 0;
    uploaded_ = false;
}

bool water_renderer::ensure_beneath(const int width, const int height) {
    return (beneath_.width() == width && beneath_.height() == height) ||
        beneath_.init(width, height, framebuffer_format{framebuffer_color::hdr, framebuffer_depth::sampled, false, false});
}

bool water_renderer::copy_beneath(const draw_target& target) {
    if (target.width <= 0 || target.height <= 0 || !ensure_beneath(target.width, target.height)) {
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, beneath_.id());
    glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, target.width, target.height);
    return true;
}

void water_renderer::bind_beneath() const {
    glActiveTexture(GL_TEXTURE0 + beneath_color_unit);
    glBindTexture(GL_TEXTURE_2D, beneath_.color_texture());
    glActiveTexture(GL_TEXTURE0 + beneath_depth_unit);
    glBindTexture(GL_TEXTURE_2D, beneath_.depth_texture());
}

void water_renderer::upload(const render_water& water, frame_profile* profile) {
    if (!uploaded_ || uploaded_revision_ != water.revision) {
        buffer_.upload(water.triangles.data(), water.triangles.size() * sizeof(render_water_vertex), profile, upload_kind::cached);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        uploaded_revision_ = water.revision;
        uploaded_ = true;
    }
}

std::vector<water_candidate> water_renderer::candidates(const render_water* water, const glm::mat4& view_proj, const glm::vec3& eye,
                                                        const water_style& style, const render_weather& weather,
                                                        frame_profile* profile) {
    if (!water || water->triangles.empty()) { return {}; }
    upload(*water, profile);
    auto found = water_candidates(*water, view_proj, eye,
                                  water_reflection_reach(style, glm::length(weather.surface_wind), weather.surface_rain));
    if (profile) { profile->work.water_candidates += found.size(); }
    while (visibility_queries_.size() < water->levels.size()) {
        GLuint query = 0;
        glGenQueries(1, &query);
        visibility_queries_.push_back(query);
    }
    visibility_shader_.set_profile(profile);
    visibility_shader_.use();
    visibility_shader_.set_mat4("u_view_proj", view_proj);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glBindVertexArray(vao_);
    for (water_candidate& candidate : found) {
        const water_level_range& range = water->levels[candidate.index];
        candidate.query = visibility_queries_[candidate.index];
        glBeginQuery(GL_SAMPLES_PASSED, candidate.query);
        glDrawArrays(GL_TRIANGLES, range.first_vertex, range.vertex_count);
        record_draw_call(profile);
        glEndQuery(GL_SAMPLES_PASSED);
    }
    glBindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    return found;
}

void water_renderer::draw_surfaces(const render_water* water, const std::vector<water_candidate>& candidates,
                                   const glm::mat4& view_proj, const draw_target& target, const water_style& style, const render_weather& weather, const std::vector<water_reflection>& reflections, const float clock_seconds,
                                   gl_timer_pool& timers, frame_profile* profile) {
    if (water == nullptr || water->triangles.empty() || surface_shader_.id() == 0 || vao_ == 0) {
        return;
    }
    if (candidates.empty()) { return; }
    timers.begin(gpu_profile_stage::water);
    upload(*water, profile);
    if (!copy_beneath(target)) {
        timers.end();
        return;
    }

    surface_shader_.set_profile(profile);
    surface_shader_.use();
    surface_shader_.set_mat4("u_view_proj", view_proj);
    surface_shader_.set_mat4("u_inverse_view_proj", glm::inverse(view_proj));
    surface_shader_.set_float("u_time", clock_seconds);
    surface_shader_.set_vec3("u_absorption", glm::vec3(style.absorption_red, style.absorption_green, style.absorption_blue));
    surface_shader_.set_vec3("u_scatter_color", style.scatter_color);
    surface_shader_.set_vec2("u_water", glm::vec2(style.max_path_meters, style.refraction_pixels));
    surface_shader_.set_vec4("u_ripples", glm::vec4(style.ripple_slope, style.ripple_wavelength_meters, style.ripple_speed,
                                                    style.ripple_fade_meters));
    surface_shader_.set_vec2("u_glint", glm::vec2(style.glint_strength, style.glint_sharpness));
    surface_shader_.set_vec2("u_wind", glm::vec2(weather.surface_wind.x, weather.surface_wind.z));
    surface_shader_.set_float("u_rain", weather.surface_rain);
    surface_shader_.set_vec4("u_wind_rough", glm::vec4(water_wind_roughness(style, glm::length(weather.surface_wind)),
                                                       style.calm_ripple_share, style.full_fetch_meters, style.fine_slope));
    surface_shader_.set_vec4("u_gusts", glm::vec4(style.gust_cell_meters, style.gust_share, style.rain_ring_slope,
                                                  style.rain_ring_meters));
    surface_shader_.set_int("u_beneath_color", beneath_color_unit);
    surface_shader_.set_int("u_beneath_depth", beneath_depth_unit);
    surface_shader_.set_int("u_reflection", reflection_unit);
    const water_reflection no_reflection;
    bind_beneath();
    glBindVertexArray(vao_);
    for (const water_candidate& candidate : candidates) {
        const water_level_range& range = water->levels[candidate.index];
        const auto reflection = std::find_if(reflections.begin(), reflections.end(),
            [&](const water_reflection& r) { return r.level == candidate.level; });
        const water_reflection& mirror = reflection != reflections.end() ? *reflection : no_reflection;
        surface_shader_.set_mat4("u_reflection_view_proj", reflection != reflections.end() ? mirror.view_proj : view_proj);
        surface_shader_.set_vec4("u_reflection_range", mirror.lookup_range);
        glActiveTexture(GL_TEXTURE0 + reflection_unit);
        glBindTexture(GL_TEXTURE_2D, mirror.texture);
        if (candidate.query != 0) { glBeginConditionalRender(candidate.query, GL_QUERY_NO_WAIT); }
        glDrawArrays(GL_TRIANGLES, range.first_vertex, range.vertex_count);
        record_draw_call(profile);
        if (candidate.query != 0) { glEndConditionalRender(); }
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    timers.end();
}

void water_renderer::draw_underwater(const glm::mat4& view_proj, const draw_target& target, const water_style& style, const float water_level,
                                     const float clock_seconds, gl_timer_pool& timers, frame_profile* profile) {
    if (underwater_shader_.id() == 0 || screen_vao_ == 0) {
        return;
    }
    timers.begin(gpu_profile_stage::water);
    if (!copy_beneath(target)) {
        timers.end();
        return;
    }
    underwater_shader_.set_profile(profile);
    underwater_shader_.use();
    underwater_shader_.set_mat4("u_inverse_view_proj", glm::inverse(view_proj));
    underwater_shader_.set_float("u_time", clock_seconds);
    underwater_shader_.set_vec3("u_absorption", glm::vec3(style.absorption_red, style.absorption_green, style.absorption_blue));
    underwater_shader_.set_vec3("u_scatter_color", style.scatter_color);
    underwater_shader_.set_vec3("u_water", glm::vec3(style.max_path_meters, style.caustic_strength, style.caustic_cell_meters));
    underwater_shader_.set_vec2("u_pond", glm::vec2(water_level, style.wobble_height_share));
    underwater_shader_.set_int("u_scene_color", beneath_color_unit);
    underwater_shader_.set_int("u_scene_depth", beneath_depth_unit);
    bind_beneath();
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    record_draw_call(profile);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    timers.end();
}
