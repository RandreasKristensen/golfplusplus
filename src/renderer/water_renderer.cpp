#include "renderer/water_renderer.h"

#include <SDL.h>

#include "renderer/gl_loader.h"
#include "renderer/render_data.h"
#include "renderer/water_light.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
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
    surfaces_.clear();
}

bool water_renderer::ensure_beneath(const int width, const int height) {
    return (beneath_.width() == width && beneath_.height() == height) ||
        beneath_.init(width, height, framebuffer_format{framebuffer_color::hdr, framebuffer_depth::sampled, false, false});
}

bool water_renderer::copy_beneath() {
    // Whatever is bound (the scene or the eyepiece's zoom) is what is copied.
    GLint bound = 0;
    GLint bounds[4] = {};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
    glGetIntegerv(GL_VIEWPORT, bounds);
    if (bounds[2] <= 0 || bounds[3] <= 0 || !ensure_beneath(bounds[2], bounds[3])) {
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(bound));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, beneath_.id());
    glBlitFramebuffer(bounds[0], bounds[1], bounds[0] + bounds[2], bounds[1] + bounds[3], 0, 0, bounds[2], bounds[3],
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
    glViewport(bounds[0], bounds[1], bounds[2], bounds[3]);
    return true;
}

void water_renderer::bind_beneath() const {
    glActiveTexture(GL_TEXTURE0 + beneath_color_unit);
    glBindTexture(GL_TEXTURE_2D, beneath_.color_texture());
    glActiveTexture(GL_TEXTURE0 + beneath_depth_unit);
    glBindTexture(GL_TEXTURE_2D, beneath_.depth_texture());
}

void water_renderer::upload(const render_water& water,frame_profile* profile) {
    if (!uploaded_ || uploaded_revision_ != water.revision) {
        std::map<float,std::vector<render_water_vertex>> levels;
        for(std::size_t i=0;i+2<water.triangles.size();i+=3) {
            auto& vertices=levels[water.triangles[i].position.y];
            vertices.insert(vertices.end(),water.triangles.begin()+static_cast<std::ptrdiff_t>(i),
                             water.triangles.begin()+static_cast<std::ptrdiff_t>(i+3));
        }
        std::vector<render_water_vertex> vertices;
        surfaces_.clear();
        for(const auto& [level,points]:levels) {
            surfaces_.push_back({level,static_cast<int>(vertices.size()),static_cast<int>(points.size())});
            vertices.insert(vertices.end(),points.begin(),points.end());
        }
        buffer_.upload(vertices.data(),vertices.size()*sizeof(render_water_vertex),profile);
        glBindBuffer(GL_ARRAY_BUFFER,0);
        uploaded_revision_ = water.revision;
        uploaded_ = true;
    }
}

std::vector<float> water_renderer::visible_levels(const render_water* water,const glm::mat4& view_proj,frame_profile* profile) {
    if(!water || water->triangles.empty()) {return {};}
    upload(*water,profile);
    while(visibility_queries_.size()<surfaces_.size()) {
        GLuint query=0;glGenQueries(1,&query);visibility_queries_.push_back(query);
    }
    const auto candidates=visible_water_levels(*water,view_proj);
    std::vector<std::size_t> issued;
    visibility_shader_.set_profile(profile);
    visibility_shader_.use();visibility_shader_.set_mat4("u_view_proj",view_proj);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glBindVertexArray(vao_);
    for(std::size_t i=0;i<surfaces_.size();++i) {
        const auto& surface=surfaces_[i];
        if(std::find(candidates.begin(),candidates.end(),surface.level)==candidates.end()) {continue;}
        glBeginQuery(GL_ANY_SAMPLES_PASSED,visibility_queries_[i]);
        glDrawArrays(GL_TRIANGLES,surface.first,surface.count);record_draw_call(profile);
        glEndQuery(GL_ANY_SAMPLES_PASSED);issued.push_back(i);
    }
    glBindVertexArray(0);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
    std::vector<float> visible;
    for(const auto i:issued) {
        GLuint seen=0;glGetQueryObjectuiv(visibility_queries_[i],GL_QUERY_RESULT,&seen);
        if(seen) {visible.push_back(surfaces_[i].level);}
    }
    return visible;
}

void water_renderer::draw_surfaces(const render_water* water, const glm::mat4& view_proj, const water_style& style,
                                   const render_weather& weather, const std::vector<water_reflection>& reflections, const float clock_seconds,
                                   gl_timer_pool& timers, frame_profile* profile) {
    if (water == nullptr || water->triangles.empty() || surface_shader_.id() == 0 || vao_ == 0) {
        return;
    }
    const auto visible = visible_water_levels(*water, view_proj);
    if (visible.empty()) { return; }
    timers.begin(gpu_profile_stage::water);
    upload(*water,profile);
    if (!copy_beneath()) {
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
    bind_beneath();
    glBindVertexArray(vao_);
    for (const surface_run& surface : surfaces_) {
        const float level = surface.level;
        if (std::find(visible.begin(),visible.end(),level)==visible.end()) {continue;}
        const auto reflection = std::find_if(reflections.begin(), reflections.end(),
            [level](const water_reflection& r) { return r.level == level; });
        surface_shader_.set_mat4("u_reflection_view_proj", reflection != reflections.end() ? reflection->view_proj : view_proj);
        glActiveTexture(GL_TEXTURE0 + reflection_unit);
        glBindTexture(GL_TEXTURE_2D, reflection != reflections.end() ? reflection->texture : 0);
        glDrawArrays(GL_TRIANGLES, surface.first, surface.count);
        record_draw_call(profile);
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    timers.end();
}

void water_renderer::draw_underwater(const glm::mat4& view_proj, const water_style& style, const float water_level,
                                     const float clock_seconds, gl_timer_pool& timers, frame_profile* profile) {
    if (underwater_shader_.id() == 0 || screen_vao_ == 0) {
        return;
    }
    timers.begin(gpu_profile_stage::water);
    if (!copy_beneath()) {
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
