#include "renderer/grass_renderer.h"
#include "renderer/gl_loader.h"
#include "renderer/shadow_map_pass.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int map_unit = 8;
constexpr int blades_per_tuft = 5;
constexpr int blocks_per_draw = 128;
constexpr float blade_top_share = 1.5f;
}

bool grass_renderer::init(const std::string& blade_vertex, const std::string& blade_fragment, const std::string& defines) {
    shutdown();
    if (!blade_shader_.load_from_files(blade_vertex, blade_fragment, defines)) { return false; }
    glGenTextures(1, &field_texture_);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (!field_texture_ || !vao_ || !vbo_) { shutdown(); return false; }
    const auto tuft = grass_tuft_vertices(blades_per_tuft);
    vertex_count_ = static_cast<int>(tuft.size());
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tuft.size() * sizeof(glm::vec3)), tuft.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void grass_renderer::shutdown() {
    if (field_texture_) { glDeleteTextures(1, &field_texture_); field_texture_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    blade_shader_.shutdown();
    uploaded_ = false;
    vertex_count_ = 0;
}

void grass_renderer::draw(const grass_frame& frame, const grass_style& style,
                          const shadow_map_pass& shadows, const int shadow_unit, gl_timer_pool& timers,
                          frame_profile* profile) {
    if (frame.terrain == nullptr || frame.field == nullptr || frame.field->samples.empty() || vao_ == 0 || style.blade_height_meters <= 0.0f) {
        return;
    }
    timers.begin(gpu_profile_stage::grass);
    const grass_field& field = *frame.field;
    if (!uploaded_ || field_revision_ != field.revision) {
        glBindTexture(GL_TEXTURE_2D, field_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, field.columns, field.rows, 0, GL_RGB, GL_FLOAT, field.samples.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uploaded_ = true;
        field_revision_ = field.revision;
    }
    const int rings = std::clamp(static_cast<int>(std::lround(style.rings)), 1, grass_max_rings);
    const view_frustum frustum = make_view_frustum(frame.view_proj);
    const std::vector<const render_static_mesh*> ground_meshes{frame.terrain, frame.material_overlay};
    const float top = blade_top_share * style.blade_height_meters * std::max(1.0f, style.heavy_blade_share);
    for (int k = 0; k < rings; ++k) {
        const float scale = std::ldexp(1.0f, k);
        const float reach = style.reach_meters * scale;
        const grass_grid grid = grass_grid_around(frame.eye, reach, style.tuft_spacing_meters * scale);
        const std::vector<glm::vec2> blocks =
            grass_visible_blocks(grid, frustum, ground_meshes, top);
        if (blocks.empty()) {
            continue;
        }
        blade_shader_.set_profile(profile);
        blade_shader_.use();
        blade_shader_.set_mat4("u_view_proj", frame.view_proj);
        blade_shader_.set_vec4("u_window", glm::vec4(field.origin, grass_field_spacing, grass_field_error));
        blade_shader_.set_vec2("u_grid", glm::vec2(grid.spacing, static_cast<float>(k)));
        blade_shader_.set_int("u_last_ring", rings - 1);
        blade_shader_.set_vec2("u_density", glm::vec2(style.reach_meters, reach));
        blade_shader_.set_vec4("u_blade", glm::vec4(style.blade_height_meters, style.blade_width_meters, 0.0f,
                                                    style.blade_sway));
        blade_shader_.set_float("u_heavy_share", style.heavy_blade_share);
        blade_shader_.set_vec3("u_rough_color", frame.rough_color);
        blade_shader_.set_vec2("u_shades", glm::vec2(style.base_shade, style.tip_shade));
        blade_shader_.set_vec2("u_wind", frame.wind);
        blade_shader_.set_float("u_time", frame.clock_seconds);
        blade_shader_.set_int("u_field", map_unit);
        glActiveTexture(GL_TEXTURE0 + map_unit);
        glBindTexture(GL_TEXTURE_2D, field_texture_);
        glActiveTexture(GL_TEXTURE0);
        shadows.bind(blade_shader_, shadow_unit);
        glBindVertexArray(vao_);
        for (std::size_t first = 0; first < blocks.size(); first += blocks_per_draw) {
            const int count = static_cast<int>(std::min<std::size_t>(blocks_per_draw, blocks.size() - first));
            std::vector<glm::vec4> corners;
            for (int i = 0; i < count; i += 2) {
                corners.emplace_back(blocks[first + i], i + 1 < count ? blocks[first + i + 1] : glm::vec2(0.0f));
            }
            blade_shader_.set_vec4_array("u_blocks", corners.data(), static_cast<int>(corners.size()));
            glDrawArraysInstanced(GL_TRIANGLES, 0, vertex_count_, count * grass_block_tufts * grass_block_tufts);
            record_draw_call(profile);
        }
        glBindVertexArray(0);
    }
    timers.end();
}
