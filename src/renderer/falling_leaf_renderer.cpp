#include "renderer/falling_leaf_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include "renderer/falling_leaf.h"
#include "renderer/gl_loader.h"

namespace {
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;
constexpr GLuint instance_turn_location = 5;
}

bool falling_leaf_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    for (const GLuint location : {instance_offset_location, instance_scale_location, instance_turn_location}) {
        glEnableVertexAttribArray(location);
        glVertexAttribDivisor(location, 1);
    }
    glBindVertexArray(0);
    return vao_ != 0;
}

void falling_leaf_renderer::shutdown() {
    shader_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void falling_leaf_renderer::draw(const std::vector<falling_leaf_run>& runs, const unsigned instance_buffer,
                                 const render_tree_instance& largest, const std::size_t trees, const glm::mat4& view_proj,
                                 const glm::vec3& eye, const float focal_pixels, const float seconds, const glm::vec2& wind,
                                 frame_profile* profile) {
    if (shader_.id() == 0 || runs.empty() || focal_pixels <= 0.0f || style_.most_per_tree <= 0) {
        return;
    }
    float most_falling = 0.0f;
    for (const falling_leaf_run& run : runs) {
        most_falling = std::max(most_falling, run.daily_share);
    }
    const float most_per_cycle = falling_leaves_per_cycle(style_, largest, most_falling, glm::length(wind));
    const int slots = std::min({style_.most_per_tree, static_cast<int>(std::ceil(most_per_cycle)),
                                static_cast<int>(static_cast<std::size_t>(style_.most_per_pass) / std::max<std::size_t>(trees, 1))});
    if (slots <= 0) {
        return;
    }
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_float("u_time", seconds);
    shader_.set_vec2("u_wind", wind);
    shader_.set_vec3("u_eye", eye);
    shader_.set_float("u_focal_pixels", focal_pixels);
    shader_.set_vec4("u_leaf", glm::vec4(style_.leaves_per_square_meter, style_.leaf_size_meters, style_.fall_speed, style_.lie_seconds));
    shader_.set_vec4("u_flutter", glm::vec4(style_.flutter_seconds, style_.flutter_meters, style_.wind_factor, style_.min_pixels));
    constexpr GLsizei stride = sizeof(render_tree_instance);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
    for (const falling_leaf_run& run : runs) {
        if (run.daily_share <= 0.0f) {
            continue;
        }
        shader_.set_vec3("u_crown_centre", run.crown.centre);
        shader_.set_vec3("u_crown_half", run.crown.half);
        shader_.set_float("u_daily_share", run.daily_share);
        shader_.set_vec3("u_leaf_color", run.leaf_color);
        shader_.set_vec3("u_withered_color", run.withered_color);
        shader_.set_float("u_withered", run.withered);
        const std::size_t first = run.first * sizeof(render_tree_instance);
        glVertexAttribPointer(instance_offset_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, offset)));
        glVertexAttribPointer(instance_scale_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, scale)));
        glVertexAttribPointer(instance_turn_location, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, yaw)));
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6 * slots, static_cast<GLsizei>(run.count));
        record_draw_call(profile);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
