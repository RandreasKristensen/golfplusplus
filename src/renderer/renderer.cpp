#include "renderer/renderer.h"

#include <SDL.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/gl_loader.h"
#include "game/text_ids.h"
#include "renderer/cart_batch.h"
#include "renderer/control_icons.h"
#include "renderer/course_map_fill.h"
#include "renderer/overlay_batch.h"
#include "renderer/overlay_pass.h"
#include "renderer/pixel_font.h"
#include "renderer/primitive_mesh.h"

namespace {
// All GL draw submissions go through these so draw calls are counted from the
// same non-global profiling sink the shader carries. Uniform sets are counted
// inside shader_program itself.
void draw_arrays(const shader_program& shader, const GLenum mode, const GLint first, const GLsizei count) {
    glDrawArrays(mode, first, count);
    record_draw_call(shader.profile());
}

void draw_elements(const shader_program& shader,
                   const GLenum mode,
                   const GLsizei count,
                   const GLenum type,
                   const void* offset) {
    glDrawElements(mode, count, type, offset);
    record_draw_call(shader.profile());
}

// Frustum-culled chunk ranges are merged into at most this many glDrawElements
// calls per mesh; culled chunks inside a merged span are drawn again rather
// than costing another draw call. Four ranges cover a ground-level view of the
// Marienlyst hub with ~0.1% extra indices over unlimited ranges.
constexpr std::size_t max_terrain_draw_ranges = 4;
// The material overlay is two orders of magnitude smaller than the terrain
// (~200 triangles), so it never deserves more than a couple of draws.
constexpr std::size_t max_material_overlay_draw_ranges = 2;

const std::vector<render_mesh_chunk>& chunks_of(const render_static_mesh* mesh) {
    static const std::vector<render_mesh_chunk> none;
    return mesh == nullptr ? none : mesh->chunks;
}

void draw_index_ranges(const shader_program& shader, const std::vector<render_index_range>& ranges) {
    for (const render_index_range& range : ranges) {
        const std::uintptr_t offset = static_cast<std::uintptr_t>(range.first_index) * sizeof(std::uint32_t);
        draw_elements(shader,
                      GL_TRIANGLES,
                      static_cast<GLsizei>(range.index_count),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<void*>(offset));
    }
}

constexpr int reference_low_res_width = 640;
constexpr int reference_low_res_height = 360;
constexpr int min_low_res_dimension = 120;

std::string asset_path(const char* relative) {
#ifdef GOLFPP_ASSETS_DIR
    std::string base = GOLFPP_ASSETS_DIR;
#else
    std::string base = "assets";
#endif
    if (!base.empty() && base.back() != '/' && base.back() != '\\') {
        base.push_back('/');
    }
    return base + relative;
}

void set_terrain_draw_state(shader_program& shader,
                            const glm::mat4& model,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const glm::vec3& color,
                            const float alpha,
                            const bool use_vertex_color) {
    shader.set_mat4("u_model", model);
    shader.set_mat4("u_mvp", proj * view * model);
    shader.set_vec3("u_color", color);
    shader.set_float("u_alpha", alpha);
    shader.set_int("u_use_vertex_color", use_vertex_color ? 1 : 0);
}

void set_terrain_draw_state(shader_program& shader,
                            const glm::mat4& model,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const glm::vec3& color,
                            const bool use_vertex_color) {
    set_terrain_draw_state(shader, model, view, proj, color, 1.0f, use_vertex_color);
}

struct primitive_geometry {
    unsigned int screen_vao = 0;
    unsigned int cylinder_vao = 0;
    unsigned int ball_vao = 0;
    int cylinder_vertex_count = 0;
    int ball_vertex_count = 0;
};

// The emote props are still immediate-mode draws (the smoke puffs blend, and
// the world marker batch is opaque-only), but they share the camera-local
// placement with the batched cart, so both read the same implementation.
glm::vec3 local_point_world(const render_data& data, const glm::vec3& local) {
    return camera_local_point(data.camera_position, data.camera_target, local);
}


void draw_local_panel(shader_program& shader,
                      const glm::mat4& view,
                      const glm::mat4& proj,
                      const render_data& data,
                      const glm::vec3& local,
                      const glm::vec3& rotation,
                      const glm::vec2& half_size,
                      const glm::vec3& color,
                      const float alpha = 1.0f) {
    const glm::mat4 model = local_panel_model(data.camera_position, data.camera_target, local, rotation, half_size);
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    draw_arrays(shader, GL_TRIANGLES, 0, 6);
}

void draw_local_cylinder(shader_program& shader,
                         const glm::mat4& view,
                         const glm::mat4& proj,
                         const render_data& data,
                         const primitive_geometry& geometry,
                         const glm::vec3& local,
                         const glm::vec3& rotation,
                         const glm::vec3& scale,
                         const glm::vec3& color,
                         const float alpha = 1.0f) {
    const glm::mat4 model = local_cylinder_model(data.camera_position, data.camera_target, local, rotation, scale);
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    glBindVertexArray(geometry.cylinder_vao);
    draw_arrays(shader, GL_TRIANGLES, 0, geometry.cylinder_vertex_count);
}

void draw_local_cylinder_between(shader_program& shader,
                                 const glm::mat4& view,
                                 const glm::mat4& proj,
                                 const render_data& data,
                                 const primitive_geometry& geometry,
                                 const glm::vec3& start_local,
                                 const glm::vec3& end_local,
                                 const float radius,
                                 const glm::vec3& color,
                                 const float alpha = 1.0f) {
    const glm::vec3 start = local_point_world(data, start_local);
    const glm::vec3 end = local_point_world(data, end_local);
    const glm::vec3 axis = end - start;
    const float length = glm::length(axis);
    if (length <= 0.0001f) {
        return;
    }

    const glm::vec3 axis_dir = axis / length;
    const glm::vec3 camera_dir = glm::normalize(data.camera_target - data.camera_position);
    const glm::vec3 reference = std::abs(glm::dot(axis_dir, camera_dir)) > 0.92f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : camera_dir;
    const glm::vec3 side = glm::normalize(glm::cross(axis_dir, reference));
    const glm::vec3 bend = glm::normalize(glm::cross(side, axis_dir));

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(side * radius, 0.0f);
    model[1] = glm::vec4(axis, 0.0f);
    model[2] = glm::vec4(bend * radius, 0.0f);
    model[3] = glm::vec4(start, 1.0f);
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    glBindVertexArray(geometry.cylinder_vao);
    draw_arrays(shader, GL_TRIANGLES, 0, geometry.cylinder_vertex_count);
}

void draw_local_sphere(shader_program& shader,
                       const glm::mat4& view,
                       const glm::mat4& proj,
                       const render_data& data,
                       const primitive_geometry& geometry,
                       const glm::vec3& local,
                       const float radius,
                       const glm::vec3& color,
                       const float alpha = 1.0f) {
    const glm::mat4 model = local_sphere_model(data.camera_position, data.camera_target, local, radius);
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    glBindVertexArray(geometry.ball_vao);
    draw_arrays(shader, GL_TRIANGLES, 0, geometry.ball_vertex_count);
}

void draw_smoke_emote_model(shader_program& shader,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const render_data& data,
                            const primitive_geometry& geometry) {
    const float t = std::max(0.0f, data.smoke_emote_elapsed);
    const float ember = 0.55f + 0.45f * std::sin(t * 24.0f);
    const glm::vec3 cigarette_local(0.08f, -0.42f, 0.78f);
    const glm::vec3 cigarette_tip(0.30f, -0.47f, 1.12f);
    draw_local_cylinder_between(shader,
                                view,
                                proj,
                                data,
                                geometry,
                                cigarette_local,
                                cigarette_tip,
                                0.026f,
                                glm::vec3(0.88f, 0.82f, 0.62f));
    draw_local_sphere(shader,
                      view,
                      proj,
                      data,
                      geometry,
                      cigarette_tip,
                      0.046f,
                      glm::vec3(0.95f, 0.20f + ember * 0.20f, 0.10f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < 5; ++i) {
        const float f = static_cast<float>(i);
        const float rise = std::fmod(t * 0.55f + f * 0.18f, 0.90f);
        const float sway = std::sin(t * 4.0f + f * 1.7f) * 0.055f;
        const float alpha = std::clamp(0.48f - rise * 0.42f, 0.04f, 0.42f);
        const float radius = 0.065f + rise * 0.075f;
        draw_local_sphere(shader,
                          view,
                          proj,
                          data,
                          geometry,
                          cigarette_tip + glm::vec3(sway + f * 0.012f, 0.10f + rise, -rise * 0.10f),
                          radius,
                          glm::vec3(0.78f, 0.80f, 0.78f),
                          alpha);
    }
    glDisable(GL_BLEND);
}

void draw_beer_emote_model(shader_program& shader,
                           const glm::mat4& view,
                           const glm::mat4& proj,
                           const render_data& data,
                           const primitive_geometry& geometry) {
    const float bob = std::sin(data.beer_emote_elapsed * 9.0f) * 0.035f;
    const glm::vec3 can_local(-0.24f, -0.50f + bob, 0.82f);
    draw_local_cylinder(shader, view, proj, data, geometry, can_local, glm::vec3(glm::radians(-8.0f), 0.0f, glm::radians(10.0f)), glm::vec3(0.145f, 0.48f, 0.145f), glm::vec3(0.76f, 0.76f, 0.70f));

    glBindVertexArray(geometry.screen_vao);
    draw_local_panel(shader, view, proj, data, can_local + glm::vec3(0.0f, 0.0f, -0.155f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.118f, 0.108f), glm::vec3(0.78f, 0.18f, 0.12f));
    draw_local_panel(shader, view, proj, data, can_local + glm::vec3(0.0f, 0.25f, -0.03f), glm::vec3(glm::radians(90.0f), 0.0f, 0.0f), glm::vec2(0.052f, 0.024f), glm::vec3(0.18f, 0.18f, 0.16f));
}

void draw_emote_world_model(shader_program& shader,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const render_data& data,
                            const primitive_geometry& geometry) {
    if (data.smoke_emote_active) {
        draw_smoke_emote_model(shader, view, proj, data, geometry);
    }
    if (data.beer_emote_active) {
        draw_beer_emote_model(shader, view, proj, data, geometry);
    }
}


void draw_control_key(overlay_batch& batch,
                      const text_assets& text,
                      const char* key,
                      const glm::vec2 center,
                      const glm::vec2 half_size,
                      const bool is_down) {
    draw_control_button_base(batch, center, half_size, is_down);
    draw_text_fitted(batch,
                     text.font,
                     find_text_style(text, is_down ? style_control_key_down : style_control_key),
                     lookup_text(text, key),
                     center,
                     half_size);
}

void draw_controls_overlay(overlay_batch& batch, const text_assets& text, const controls_overlay_state& controls) {
    if (!controls.visible) {
        return;
    }

    const glm::vec2 dpad_half(0.060f, 0.060f);
    const glm::vec2 small_half(0.066f, 0.052f);
    const glm::vec2 wide_half(0.138f, 0.052f);
    const glm::vec2 dpad_center(0.79f, 0.02f);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.0f, 0.126f), dpad_half, controls.up_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.0f, 0.126f), glm::vec2(0.0f, 1.0f), dpad_half, controls.up_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(-0.070f, 0.0f), dpad_half, controls.left_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(-0.070f, 0.0f), glm::vec2(-1.0f, 0.0f), dpad_half, controls.left_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.070f, 0.0f), dpad_half, controls.right_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.070f, 0.0f), glm::vec2(1.0f, 0.0f), dpad_half, controls.right_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.0f, -0.126f), dpad_half, controls.down_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.0f, -0.126f), glm::vec2(0.0f, -1.0f), dpad_half, controls.down_down);

    draw_control_button_base(batch, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);
    draw_space_icon(batch, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);

    draw_control_button_base(batch, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);
    draw_shift_icon(batch, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);

    draw_control_button_base(batch, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);
    draw_enter_icon(batch, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);

    draw_control_button_base(batch, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);
    draw_backspace_icon(batch, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);

    draw_control_button_base(batch, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);
    draw_retee_icon(batch, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);

    const glm::vec2 key_half(0.052f, 0.046f);
    draw_control_key(batch, text, text_controls_key_1, glm::vec2(0.70f, -0.74f), key_half, controls.key_1_down);
    draw_control_key(batch, text, text_controls_key_2, glm::vec2(0.88f, -0.74f), key_half, controls.key_2_down);
}


void draw_fps_counter(overlay_batch& batch, const text_assets& text, const std::string& label) {
    if (label.empty()) {
        return;
    }

    draw_text(batch, text.font, find_text_style(text, style_debug_fps), label, glm::vec2(-0.96f, 0.92f));
}

// Developer diagnostics: the lines come from profiling, not the string table.
void draw_profile_overlay(overlay_batch& batch, const text_assets& text, const frame_profile& profile) {
    const std::vector<std::string> lines = format_profile_overlay_lines(profile);
    if (lines.empty()) {
        return;
    }

    constexpr float line_step = 0.052f;
    const text_style& style = find_text_style(text, style_debug_profile);
    glm::vec2 cursor(-0.96f, 0.855f);
    for (const std::string& line : lines) {
        draw_text(batch, text.font, style, line, cursor);
        cursor.y -= line_step;
    }
}

void draw_club_label(overlay_batch& batch, const text_assets& text, const std::string& label) {
    const glm::vec3 panel_color(0.055f, 0.06f, 0.07f);
    const glm::vec2 panel_center(0.78f, 0.78f);
    const glm::vec2 panel_half(0.17f, 0.12f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color);

    draw_text_fitted(batch, text.font, find_text_style(text, style_hud_club), label, panel_center, panel_half);
}

void draw_interact_prompt(overlay_batch& batch) {
    const glm::vec3 prompt_color(0.95f, 0.82f, 0.28f);
    draw_space_icon(batch, glm::vec2(0.0f, -0.56f), glm::vec2(0.16f, 0.075f), prompt_color, 1.0f);
}

void draw_power_meter(overlay_batch& batch, const text_assets& text, const float swing_power) {
    const float power = std::clamp(swing_power, 0.0f, 1.0f);
    const glm::vec3 panel_color(0.055f, 0.060f, 0.065f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const glm::vec3 amber(0.92f, 0.70f, 0.18f);

    const glm::vec2 panel_center(-0.62f, -0.72f);
    const glm::vec2 panel_half(0.32f, 0.155f);
    draw_overlay_quad(batch, panel_center + glm::vec2(0.012f, -0.014f), panel_half, glm::vec3(0.0f), 0.22f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color, 0.78f);
    draw_button_outline(batch, panel_center, panel_half, outline_color, 0.54f);

    draw_text(batch, text.font, find_text_style(text, style_hud_label), lookup_text(text, text_hud_power), panel_center + glm::vec2(-0.275f, 0.105f));

    const glm::vec2 track_center = panel_center + glm::vec2(0.020f, -0.012f);
    const glm::vec2 track_half(0.245f, 0.035f);
    const float track_left = track_center.x - track_half.x;
    const float track_right = track_center.x + track_half.x;
    const float track_width = track_half.x * 2.0f;

    draw_overlay_quad(batch, track_center, track_half, glm::vec3(0.030f, 0.032f, 0.034f), 0.92f);
    draw_button_outline(batch, track_center, track_half, outline_color, 0.56f);

    if (power > 0.0f) {
        const float fill_half_width = track_half.x * power;
        const glm::vec2 fill_center(track_left + fill_half_width, track_center.y);
        draw_overlay_quad(batch, fill_center, glm::vec2(fill_half_width, track_half.y * 0.58f), amber, 0.92f);
    }

    const float sweet_x = track_left + track_width * 0.86f;
    draw_overlay_segment(batch,
                         glm::vec2(sweet_x, track_center.y - track_half.y * 1.08f),
                         glm::vec2(sweet_x, track_center.y + track_half.y * 1.08f),
                         0.006f,
                         glm::vec3(0.96f, 0.88f, 0.55f),
                         0.88f);

    const float needle_x = track_left + track_width * power;
    draw_overlay_segment(batch,
                         glm::vec2(needle_x, track_center.y - 0.055f),
                         glm::vec2(needle_x, track_center.y + 0.055f),
                         0.008f,
                         glm::vec3(0.98f, 0.80f, 0.24f),
                         0.96f);

    const std::array<float, 3> ticks{0.0f, 0.5f, 1.0f};
    for (const float tick : ticks) {
        const float x = track_left + track_width * tick;
        draw_overlay_segment(batch,
                             glm::vec2(x, track_center.y - 0.058f),
                             glm::vec2(x, track_center.y - 0.080f),
                             0.005f,
                             outline_color,
                             0.72f);
    }

    const text_style& scale_style = find_text_style(text, style_hud_scale);
    const float scale_y = panel_center.y - 0.112f;
    draw_text(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_min), glm::vec2(track_left, scale_y));
    draw_text(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_mid), glm::vec2(track_center.x, scale_y));
    draw_text(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_max), glm::vec2(track_right, scale_y));
}

void draw_cart_hud(overlay_batch& batch, const text_assets& text, const render_data& data) {
    if (!data.cart_active) {
        return;
    }

    const glm::vec3 panel_color(0.055f, 0.060f, 0.065f);
    const glm::vec3 outline_color(0.60f, 0.62f, 0.56f);
    const glm::vec3 meter_color = data.cart_drifting ? glm::vec3(0.96f, 0.56f, 0.22f) : glm::vec3(0.72f, 0.80f, 0.36f);

    const glm::vec2 panel_center(-0.74f, 0.72f);
    const glm::vec2 panel_half(0.19f, 0.13f);
    draw_overlay_quad(batch, panel_center + glm::vec2(0.012f, -0.012f), panel_half, glm::vec3(0.0f), 0.18f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color, 0.82f);
    draw_button_outline(batch, panel_center, panel_half, outline_color, 0.52f);

    draw_text(batch, text.font, find_text_style(text, style_cart_label), lookup_text(text, text_hud_cart), panel_center + glm::vec2(-0.150f, 0.082f));
    draw_text(batch,
              text.font,
              find_text_style(text, data.cart_drifting ? style_cart_drift : style_cart_drive),
              lookup_text(text, data.cart_drifting ? text_hud_cart_drift : text_hud_cart_drive),
              panel_center + glm::vec2(-0.150f, 0.020f));

    const glm::vec2 track_center = panel_center + glm::vec2(0.030f, -0.046f);
    const glm::vec2 track_half(0.120f, 0.024f);
    draw_overlay_quad(batch, track_center, track_half, glm::vec3(0.028f, 0.030f, 0.032f), 0.92f);
    draw_button_outline(batch, track_center, track_half, outline_color, 0.46f);

    const float speed_ratio = std::clamp(data.cart_speed / 18.0f, 0.0f, 1.0f);
    if (speed_ratio > 0.0f) {
        const float fill_half_width = track_half.x * speed_ratio;
        draw_overlay_quad(batch,
                          glm::vec2(track_center.x - track_half.x + fill_half_width, track_center.y),
                          glm::vec2(fill_half_width, track_half.y * 0.60f),
                          meter_color,
                          0.94f);
    }
}

void draw_skills_panel(overlay_batch& batch, const text_assets& text, const std::vector<render_skill_progress>& skills) {
    if (skills.empty()) {
        return;
    }

    const glm::vec2 center(0.0f, 0.16f);
    const glm::vec2 half(0.58f, 0.44f);
    const glm::vec3 panel_color(0.050f, 0.055f, 0.060f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const text_style& header = find_text_style(text, style_panel_header);
    const text_style& value = find_text_style(text, style_panel_value);

    draw_overlay_quad(batch, center + glm::vec2(0.018f, -0.020f), half, glm::vec3(0.0f), 0.32f);
    draw_overlay_quad(batch, center, half, panel_color, 0.88f);
    draw_button_outline(batch, center, half, outline_color, 0.64f);

    draw_text(batch, text.font, find_text_style(text, style_panel_title), lookup_text(text, text_skills_title), center + glm::vec2(0.0f, half.y - 0.070f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_name), center + glm::vec2(-0.48f, half.y - 0.145f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_level), center + glm::vec2(0.02f, half.y - 0.145f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_xp), center + glm::vec2(0.16f, half.y - 0.145f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_next), center + glm::vec2(0.36f, half.y - 0.145f));

    constexpr float row_gap = 0.105f;
    for (std::size_t i = 0; i < skills.size(); ++i) {
        const render_skill_progress& skill = skills[i];
        const float y = center.y + half.y - 0.225f - static_cast<float>(i) * row_gap;
        const glm::vec3 row_color = i % 2 == 0 ? glm::vec3(0.075f, 0.080f, 0.078f) : glm::vec3(0.060f, 0.064f, 0.066f);
        draw_overlay_quad(batch, glm::vec2(center.x, y - 0.016f), glm::vec2(0.50f, 0.038f), row_color, 0.74f);
        draw_text(batch, text.font, value, skill.label, glm::vec2(center.x - 0.48f, y));
        draw_text(batch, text.font, value, std::to_string(std::max(1, skill.level)), glm::vec2(center.x + 0.02f, y));
        draw_text(batch, text.font, value, std::to_string(std::max(0, skill.xp)), glm::vec2(center.x + 0.16f, y));
        draw_text(batch, text.font, value, std::to_string(std::max(0, skill.xp_to_next)), glm::vec2(center.x + 0.36f, y));
    }

    draw_text(batch, text.font, find_text_style(text, style_panel_hint), lookup_text(text, text_skills_hint), center + glm::vec2(0.0f, -half.y + 0.055f));
}

void draw_xp_icon_cell(overlay_batch& batch,
                       const glm::vec2 icon_center,
                       const int x,
                       const int y,
                       const float cell_size,
                       const glm::vec3 color,
                       const float alpha) {
    draw_overlay_quad(batch,
                      icon_center + glm::vec2((static_cast<float>(x) - 2.0f) * cell_size,
                                              (2.0f - static_cast<float>(y)) * cell_size),
                      glm::vec2(cell_size * 0.43f),
                      color,
                      alpha);
}

void draw_xp_icon_pattern(overlay_batch& batch,
                          const glm::vec2 icon_center,
                          const std::array<const char*, 5>& pattern,
                          const float cell_size,
                          const glm::vec3 color,
                          const float alpha) {
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (pattern[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '#') {
                draw_xp_icon_cell(batch, icon_center, x, y, cell_size, color, alpha);
            }
        }
    }
}

void draw_xp_icon(overlay_batch& batch,
                  const skill_icon_id icon,
                  const glm::vec2 center,
                  const float alpha) {
    const float cell = 0.010f;
    switch (icon) {
    case skill_icon_id::golf_swing:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{"..#..", "..#..", ".##..", "...#.", "...##"}},
                             cell,
                             glm::vec3(0.92f, 0.78f, 0.38f),
                             alpha);
        draw_overlay_quad(batch, center + glm::vec2(-0.016f, -0.020f), glm::vec2(0.008f), glm::vec3(0.92f), alpha);
        break;
    case skill_icon_id::smoking:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{".....", ".###.", "...#.", "####.", "....."}},
                             cell,
                             glm::vec3(0.90f, 0.84f, 0.66f),
                             alpha);
        draw_xp_icon_cell(batch, center, 4, 2, cell, glm::vec3(0.86f, 0.38f, 0.22f), alpha);
        break;
    case skill_icon_id::fitness:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{".....", ".##..", ".###.", "..###", "....."}},
                             cell,
                             glm::vec3(0.52f, 0.82f, 0.58f),
                             alpha);
        break;
    case skill_icon_id::generic:
    default:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{"..#..", ".###.", "#####", ".###.", "..#.."}},
                             cell,
                             glm::vec3(0.70f, 0.78f, 0.92f),
                             alpha);
        break;
    }
}

void draw_xp_drops(overlay_batch& batch, const text_assets& text, const std::vector<render_xp_drop>& drops) {
    if (drops.empty()) {
        return;
    }

    const glm::vec3 panel_color(0.035f, 0.040f, 0.040f);
    const glm::vec3 outline_color(0.68f, 0.70f, 0.56f);
    const text_style& label_style = find_text_style(text, style_xp_drop);
    const glm::vec2 row_half(0.155f, 0.043f);
    const float row_gap = 0.092f;
    const float right = 0.585f;
    const float start_y = 0.565f;
    const float travel_y = 0.62f;

    for (std::size_t i = 0; i < drops.size(); ++i) {
        const render_xp_drop& drop = drops[i];
        if (drop.xp <= 0) {
            continue;
        }

        const float progress = drop.lifetime <= 0.0f ? 1.0f : std::clamp(drop.age / drop.lifetime, 0.0f, 1.0f);
        const float alpha = progress > 0.78f ? std::max(0.16f, 1.0f - (progress - 0.78f) / 0.22f) : 1.0f;
        const glm::vec2 center(right - row_half.x, start_y - static_cast<float>(i) * row_gap + progress * travel_y);

        draw_overlay_quad(batch, center + glm::vec2(0.010f, -0.010f), row_half, glm::vec3(0.0f), 0.18f * alpha);
        draw_overlay_quad(batch, center, row_half, panel_color, 0.72f * alpha);
        draw_button_outline(batch, center, row_half, outline_color, 0.42f * alpha);
        draw_xp_icon(batch, drop.icon, center + glm::vec2(-0.105f, 0.0f), alpha);

        const std::string label = format_text(text, text_hud_xp_drop, {{"xp", std::to_string(std::max(0, drop.xp))}});
        draw_text_fitted(batch,
                         text.font,
                         with_color(label_style, label_style.color * alpha),
                         label,
                         center + glm::vec2(-0.055f, 0.021f),
                         glm::vec2(0.100f, 0.030f));
    }
}


bool project_to_screen(const glm::mat4& view,
                       const glm::mat4& proj,
                       const glm::vec3& world_position,
                       glm::vec2& screen_position) {
    const glm::vec4 clip = proj * view * glm::vec4(world_position, 1.0f);
    if (clip.w <= 0.0f) {
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }

    screen_position = glm::vec2(ndc.x, ndc.y);
    return true;
}

void draw_rangefinder_view(overlay_batch& batch,
                           const text_assets& text,
                           const glm::mat4& view,
                           const glm::mat4& proj,
                           const render_data& data) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.16f, 0.34f, 0.24f), 0.18f);
    draw_overlay_quad(batch, glm::vec2(-0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(batch, glm::vec2(0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);
    draw_overlay_quad(batch, glm::vec2(0.0f, -0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);

    const glm::vec3 reticle_color(0.62f, 0.96f, 0.64f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.006f, 0.11f), reticle_color, 0.72f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.11f, 0.006f), reticle_color, 0.72f);
    draw_overlay_quad(batch, glm::vec2(-0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.0f, -0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);

    glm::vec2 pin_screen(0.0f);
    const glm::vec3 label_anchor = data.pin_position + glm::vec3(0.0f, data.pin_visual_height_meters + 0.36f, 0.0f);
    if (!project_to_screen(view, proj, label_anchor, pin_screen)) {
        return;
    }

    pin_screen.x = std::max(-0.78f, std::min(0.78f, pin_screen.x));
    pin_screen.y = std::max(-0.68f, std::min(0.82f, pin_screen.y));
    const glm::vec2 label_max_half(0.22f, 0.065f);
    const text_style& style = find_text_style(text, style_rangefinder);
    const float pixel_size = fitted_pixel_size(text.font, style, data.rangefinder_distance_label, label_max_half);
    const float label_width = pixel_text_width(text.font, data.rangefinder_distance_label, pixel_size);
    const glm::vec2 panel_half(std::max(0.12f, label_width * 0.5f + 0.035f), 0.070f);
    draw_overlay_quad(batch, pin_screen + glm::vec2(0.0f, 0.015f), panel_half, glm::vec3(0.015f, 0.032f, 0.024f), 0.78f);
    draw_text(batch,
              text.font,
              with_pixel_size(style, pixel_size),
              data.rangefinder_distance_label,
              pin_screen + glm::vec2(0.0f, 0.015f));
}

void expand_map_bounds(const glm::vec3& position, glm::vec2& min_point, glm::vec2& max_point) {
    min_point.x = std::min(min_point.x, position.x);
    min_point.y = std::min(min_point.y, position.z);
    max_point.x = std::max(max_point.x, position.x);
    max_point.y = std::max(max_point.y, position.z);
}

course_map_layout make_course_map_layout(const render_data& data) {
    glm::vec2 min_point(data.tee_position.x, data.tee_position.z);
    glm::vec2 max_point = min_point;
    expand_map_bounds(data.pin_position, min_point, max_point);
    expand_map_bounds(data.player_position, min_point, max_point);
    expand_map_bounds(data.ball_position, min_point, max_point);

    if (data.terrain_mesh != nullptr && data.terrain_mesh->bounds.valid) {
        // Per-axis min/max of the cached AABB equals expanding by every vertex.
        expand_map_bounds(data.terrain_mesh->bounds.min, min_point, max_point);
        expand_map_bounds(data.terrain_mesh->bounds.max, min_point, max_point);
    }

    for (const render_tree& tree : render_trees(data)) {
        expand_map_bounds(tree.base, min_point, max_point);
        expand_map_bounds(tree.base + glm::vec3(tree.leaf_radius, 0.0f, tree.leaf_radius), min_point, max_point);
        expand_map_bounds(tree.base - glm::vec3(tree.leaf_radius, 0.0f, tree.leaf_radius), min_point, max_point);
    }

    const glm::vec2 size = max_point - min_point;
    const float fallback_span = std::max(1.0f, data.course_extent);
    const float span = std::max(fallback_span * 0.12f, std::max(size.x, size.y));

    course_map_layout layout;
    layout.world_center = glm::vec3((min_point.x + max_point.x) * 0.5f,
                                    0.0f,
                                    (min_point.y + max_point.y) * 0.5f);
    layout.scale = std::min(layout.half_size.x, layout.half_size.y) * 1.72f / std::max(1.0f, span);
    return layout;
}

void draw_map_marker(overlay_batch& batch,
                     const glm::vec2 position,
                     const glm::vec3 color,
                     const float radius) {
    draw_overlay_quad(batch, position, glm::vec2(radius), glm::vec3(0.04f, 0.035f, 0.025f), 0.55f);
    draw_overlay_quad(batch, position, glm::vec2(radius * 0.64f), color, 1.0f);
}

// The terrain fill is retained (see renderer/course_map_fill.h): it is drawn
// from its own buffer between the paper background and the map markers, so
// submission order — and therefore blending — is unchanged.
void draw_paper_course_map(overlay_pass& pass, course_map_fill_cache& fill_cache, const render_data& data) {
    overlay_batch& batch = pass.batch();
    const course_map_layout layout = make_course_map_layout(data);
    const glm::vec3 paper(0.76f, 0.72f, 0.55f);
    const glm::vec3 paper_shadow(0.14f, 0.12f, 0.09f);
    draw_overlay_quad(batch, layout.center + glm::vec2(0.035f, -0.035f), layout.half_size, paper_shadow, 0.42f);
    draw_overlay_quad(batch, layout.center, layout.half_size, paper, 0.96f);

    const glm::vec3 fold(0.42f, 0.35f, 0.24f);
    draw_overlay_quad(batch, layout.center, glm::vec2(layout.half_size.x, 0.006f), fold, 0.18f);
    draw_overlay_quad(batch, layout.center, glm::vec2(0.006f, layout.half_size.y), fold, 0.18f);
    draw_overlay_quad(batch,
                      layout.center + glm::vec2(0.0f, layout.half_size.y),
                      glm::vec2(layout.half_size.x, 0.012f),
                      fold,
                      0.46f);
    draw_overlay_quad(batch,
                      layout.center - glm::vec2(0.0f, layout.half_size.y),
                      glm::vec2(layout.half_size.x, 0.012f),
                      fold,
                      0.46f);
    draw_overlay_quad(batch,
                      layout.center + glm::vec2(layout.half_size.x, 0.0f),
                      glm::vec2(0.012f, layout.half_size.y),
                      fold,
                      0.46f);
    draw_overlay_quad(batch,
                      layout.center - glm::vec2(layout.half_size.x, 0.0f),
                      glm::vec2(0.012f, layout.half_size.y),
                      fold,
                      0.46f);

    update_course_map_fill_cache(fill_cache, layout, data.terrain_mesh);
    pass.draw_retained(fill_cache.fill.vertices, fill_cache.revision);

    for (const render_tree& tree : render_trees(data)) {
        const glm::vec2 p = map_point(layout, tree.base);
        const float radius = std::max(0.012f, std::min(0.028f, tree.leaf_radius * layout.scale));
        draw_map_marker(batch, p, glm::vec3(0.08f, 0.24f, 0.11f), radius);
    }

    draw_map_marker(batch, map_point(layout, data.tee_position), glm::vec3(0.34f, 0.21f, 0.12f), 0.023f);
    draw_map_marker(batch, map_point(layout, data.ball_position), glm::vec3(0.94f, 0.93f, 0.82f), 0.020f);
    draw_map_marker(batch, map_point(layout, data.player_position), glm::vec3(0.22f, 0.46f, 0.72f), 0.024f);

    const glm::vec2 pin = map_point(layout, data.pin_position);
    draw_overlay_quad(batch, pin + glm::vec2(0.0f, 0.028f), glm::vec2(0.004f, 0.042f), glm::vec3(0.06f, 0.04f, 0.025f), 0.92f);
    draw_overlay_quad(batch, pin + glm::vec2(0.022f, 0.055f), glm::vec2(0.028f, 0.018f), glm::vec3(0.76f, 0.17f, 0.12f), 0.96f);
    draw_map_marker(batch, pin, glm::vec3(0.94f, 0.78f, 0.22f), 0.018f);
}

std::string score_label(const scorecard_row& row) {
    return row.played ? std::to_string(row.strokes) : "";
}

std::string relative_label(const scorecard_row& row) {
    return row.played ? row.relative_label : "";
}

std::string hole_label(const text_assets& text, const scorecard_row& row, const bool compact) {
    if (compact || row.hole_name.empty()) {
        return std::to_string(row.hole_number);
    }
    return format_text(text, text_scorecard_hole_row, {{"hole", std::to_string(row.hole_number)}, {"name", row.hole_name}});
}

// Scorecard cells are centred in their column; the first column is left
// aligned but vertically centred on the row like the others.
glm::vec2 left_cell_anchor(const pixel_font_data& font, const float x, const float y, const float pixel_size) {
    return glm::vec2(x, y + static_cast<float>(font.height) * 0.5f * pixel_size);
}

void draw_paper_card_base(overlay_batch& batch,
                          const glm::vec2 center,
                          const glm::vec2 half_size,
                          const float alpha) {
    const glm::vec3 paper(0.78f, 0.73f, 0.56f);
    const glm::vec3 paper_shadow(0.12f, 0.095f, 0.065f);
    const glm::vec3 fold(0.42f, 0.35f, 0.24f);

    draw_overlay_quad(batch, center + glm::vec2(0.030f, -0.034f), half_size, paper_shadow, 0.40f * alpha);
    draw_overlay_quad(batch, center, half_size, paper, 0.97f * alpha);
    draw_overlay_quad(batch, center + glm::vec2(-half_size.x * 0.28f, 0.0f), glm::vec2(0.004f, half_size.y), fold, 0.14f * alpha);
    draw_overlay_quad(batch, center + glm::vec2(half_size.x * 0.22f, 0.0f), glm::vec2(0.003f, half_size.y), fold, 0.10f * alpha);
    draw_button_outline(batch, center, half_size, fold, 0.44f * alpha);
}

void draw_scorecard_grid_lines(overlay_batch& batch,
                               const glm::vec2 center,
                               const glm::vec2 half_size,
                               const std::array<float, 5>& x_edges,
                               const float header_y,
                               const float row_height,
                               const int row_count,
                               const float alpha) {
    const glm::vec3 ink(0.23f, 0.19f, 0.13f);
    const float top = header_y + row_height * 0.62f;
    const float bottom = header_y - row_height * (static_cast<float>(row_count) + 0.62f);
    for (float x : x_edges) {
        draw_overlay_segment(batch, glm::vec2(x, top), glm::vec2(x, bottom), 0.004f, ink, 0.38f * alpha);
    }
    for (int i = 0; i <= row_count + 1; ++i) {
        const float y = top - static_cast<float>(i) * row_height;
        draw_overlay_segment(batch, glm::vec2(x_edges.front(), y), glm::vec2(x_edges.back(), y), 0.004f, ink, 0.34f * alpha);
    }
    draw_button_outline(batch, center, half_size, ink, 0.22f * alpha);
}

void draw_scorecard_headers(overlay_batch& batch,
                            const text_assets& text,
                            const std::array<float, 5>& x_edges,
                            const float y,
                            const text_style& style) {
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_hole), glm::vec2((x_edges[0] + x_edges[1]) * 0.5f, y));
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y));
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_score), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y));
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_relative), glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y));
}

void draw_scorecard_row_text(overlay_batch& batch,
                             const text_assets& text,
                             const scorecard_row& row,
                             const std::array<float, 5>& x_edges,
                             const float y,
                             const float pixel,
                             const bool compact,
                             const bool current) {
    const text_style ink = with_pixel_size(find_text_style(text, current ? style_scorecard_row_current : style_scorecard_row), pixel);
    const text_style score_ink = row.played ? ink : with_pixel_size(find_text_style(text, style_scorecard_row_pending), pixel);
    const std::string hole = hole_label(text, row, compact);
    const float hole_pixel = fitted_pixel_size(text.font, ink, hole, glm::vec2((x_edges[1] - x_edges[0]) * 0.48f, 0.030f));
    draw_text(batch,
              text.font,
              with_align(with_pixel_size(ink, hole_pixel), text_align::left),
              hole,
              left_cell_anchor(text.font, x_edges[0] + 0.014f, y, hole_pixel));
    draw_text(batch, text.font, ink, std::to_string(row.par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y));
    draw_text(batch, text.font, score_ink, score_label(row), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y));
    draw_text(batch, text.font, score_ink, relative_label(row), glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y));
}

void draw_scorecard_totals(overlay_batch& batch,
                           const text_assets& text,
                           const scorecard_data& scorecard,
                           const std::array<float, 5>& x_edges,
                           const float y,
                           const text_style& style) {
    draw_text(batch,
              text.font,
              with_align(style, text_align::left),
              lookup_text(text, text_scorecard_total),
              left_cell_anchor(text.font, x_edges[0] + 0.014f, y, style.pixel_size));
    draw_text(batch, text.font, style, std::to_string(scorecard.total_par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y));
    draw_text(batch, text.font, style, std::to_string(scorecard.total_strokes), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y));
    draw_text(batch, text.font, style, scorecard.total_relative_label, glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y));
}

void draw_scorecard_card(overlay_batch& batch,
                         const text_assets& text,
                         const scorecard_data& scorecard,
                         const glm::vec2 center,
                         const glm::vec2 half_size,
                         const bool compact) {
    if (scorecard.rows.empty()) {
        return;
    }

    draw_paper_card_base(batch, center, half_size, 1.0f);

    draw_text_fitted(batch,
                     text.font,
                     find_text_style(text, compact ? style_scorecard_title_compact : style_scorecard_title),
                     scorecard.course_name,
                     center + glm::vec2(0.0f, half_size.y - 0.070f),
                     glm::vec2(half_size.x * 0.78f, 0.055f));
    draw_text(batch,
              text.font,
              find_text_style(text, compact ? style_scorecard_subtitle_compact : style_scorecard_subtitle),
              lookup_text(text, compact ? text_scorecard_title : text_scorecard_results_title),
              center + glm::vec2(0.0f, half_size.y - (compact ? 0.126f : 0.142f)));

    const std::size_t row_limit = compact ? std::min<std::size_t>(scorecard.rows.size(), 8U) : scorecard.rows.size();
    std::size_t first_row = 0;
    if (compact && scorecard.rows.size() > row_limit) {
        const std::size_t current = std::min(scorecard.current_hole_index, scorecard.rows.size() - 1U);
        const std::size_t preferred = current > 3U ? current - 3U : 0U;
        first_row = std::min(preferred, scorecard.rows.size() - row_limit);
    }

    const float grid_top = center.y + half_size.y - (compact ? 0.180f : 0.220f);
    const float grid_bottom = center.y - half_size.y + (compact ? 0.118f : 0.142f);
    const float available = std::max(0.12f, grid_top - grid_bottom);
    const float row_height = std::min(compact ? 0.056f : 0.060f, available / static_cast<float>(row_limit + 1U));
    const float header_y = grid_top - row_height * 0.50f;
    const float x0 = center.x - half_size.x + 0.052f;
    const float x4 = center.x + half_size.x - 0.052f;
    const float hole_width = compact ? (x4 - x0) * 0.28f : (x4 - x0) * 0.46f;
    const float par_width = (x4 - x0) * 0.16f;
    const float score_width = (x4 - x0) * 0.22f;
    const std::array<float, 5> x_edges{{
        x0,
        x0 + hole_width,
        x0 + hole_width + par_width,
        x0 + hole_width + par_width + score_width,
        x4
    }};

    draw_scorecard_grid_lines(batch, center, half_size, x_edges, header_y, row_height, static_cast<int>(row_limit), 1.0f);
    draw_scorecard_headers(batch,
                           text,
                           x_edges,
                           header_y,
                           find_text_style(text, compact ? style_scorecard_header_compact : style_scorecard_header));

    // Rows shrink with the row height on long courses.
    const float max_row_pixel = find_text_style(text, compact ? style_scorecard_row_compact : style_scorecard_row).pixel_size;
    const float row_pixel = std::min(max_row_pixel, row_height * 0.18f);
    for (std::size_t row_index = 0; row_index < row_limit; ++row_index) {
        const std::size_t source_index = first_row + row_index;
        const scorecard_row& row = scorecard.rows[source_index];
        const float y = header_y - row_height * (static_cast<float>(row_index) + 1.0f);
        const bool current = !scorecard.finished && source_index == scorecard.current_hole_index;
        draw_scorecard_row_text(batch, text, row, x_edges, y, row_pixel, compact, current);
    }

    const float total_y = center.y - half_size.y + (compact ? 0.060f : 0.076f);
    draw_scorecard_totals(batch,
                          text,
                          scorecard,
                          x_edges,
                          total_y,
                          find_text_style(text, compact ? style_scorecard_total_compact : style_scorecard_total));

    if (!compact) {
        draw_text(batch,
                  text.font,
                  find_text_style(text, style_scorecard_hint),
                  lookup_text(text, text_scorecard_results_hint),
                  center + glm::vec2(0.0f, -half_size.y + 0.034f));
    }
}

void draw_compact_scorecard(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_scorecard_card(batch, text, scorecard, glm::vec2(-0.48f, 0.32f), glm::vec2(0.44f, 0.44f), true);
}

void draw_course_results(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.015f, 0.013f, 0.012f), 0.70f);
    draw_scorecard_card(batch, text, scorecard, glm::vec2(0.0f, 0.0f), glm::vec2(0.74f, 0.80f), false);
}

void draw_stroke_ticks(overlay_batch& batch, const int stroke_count) {
    // These used to inherit u_alpha from the previous overlay quad; the club
    // label panel and its text (always drawn just before) leave it at 1.0.
    const int strokes = std::max(stroke_count, 0);
    const int ten_marks = std::min(strokes / 10, 8);
    for (int i = 0; i < ten_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.07f;
        draw_overlay_quad(batch, glm::vec2(x, 0.86f), glm::vec2(0.026f, 0.08f), glm::vec3(0.92f, 0.70f, 0.18f), 1.0f);
    }

    const int one_marks = strokes % 10;
    for (int i = 0; i < one_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.055f;
        draw_overlay_quad(batch, glm::vec2(x, 0.76f), glm::vec2(0.015f, 0.06f), glm::vec3(0.88f, 0.88f, 0.78f), 1.0f);
    }
}

// The debug overlay is flushed as its own draw so its GL cost can be moved
// into the DBGUI bucket: flush what is queued underneath it, mark, queue and
// flush the debug text, reclaim. Layering is unchanged; showing it costs one
// extra overlay draw call (the split) in the frame counters.
void draw_debug_overlay(overlay_pass& pass, const text_assets& text, const render_data& data, frame_profile* profile) {
    pass.flush();
    const debug_overlay_cost_mark mark = mark_debug_overlay_cost(profile);
    draw_fps_counter(pass.batch(), text, data.fps_label);
    draw_profile_overlay(pass.batch(), text, data.profile_summary);
    pass.flush();
    reclaim_debug_overlay_cost(profile, mark);
}
}

bool renderer::init(SDL_Window* window) {
    window_ = window;
    if (!window_) {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    if (!init_framebuffer()) {
        return false;
    }

    if (!init_shaders()) {
        return false;
    }

    if (!init_geometry()) {
        return false;
    }

    const std::string tree_vert = asset_path("shaders/tree_instanced.vert");
    const std::string tree_frag = asset_path("shaders/terrain.frag");
    if (!tree_renderer_.init(tree_vert.c_str(),
                             tree_frag.c_str(),
                             tree_renderer::mesh_source{cylinder_vbo_, cylinder_vertex_count_},
                             tree_renderer::mesh_source{cone_vbo_, cone_vertex_count_})) {
        return false;
    }

    const std::string world_marker_vert = asset_path("shaders/world_marker.vert");
    const std::string world_marker_frag = asset_path("shaders/world_marker.frag");
    if (!world_marker_renderer_.init(world_marker_vert.c_str(), world_marker_frag.c_str())) {
        return false;
    }

    const std::string overlay_vert = asset_path("shaders/overlay.vert");
    const std::string overlay_frag = asset_path("shaders/overlay.frag");
    if (!overlay_pass_.init(overlay_vert.c_str(), overlay_frag.c_str())) {
        return false;
    }

    // Optional: profiling still works fully on the CPU side if this fails.
    gpu_timers_.init();

    return true;
}

void renderer::shutdown() {
    gpu_timers_.shutdown();
    tree_renderer_.shutdown();
    world_marker_renderer_.shutdown();
    overlay_pass_.shutdown();
    terrain_shader_.shutdown();
    ball_shader_.shutdown();
    crt_shader_.shutdown();

    if (ground_vbo_ != 0) {
        glDeleteBuffers(1, &ground_vbo_);
        ground_vbo_ = 0;
    }

    if (ground_vao_ != 0) {
        glDeleteVertexArrays(1, &ground_vao_);
        ground_vao_ = 0;
    }

    if (terrain_mesh_ebo_ != 0) {
        glDeleteBuffers(1, &terrain_mesh_ebo_);
        terrain_mesh_ebo_ = 0;
    }

    if (terrain_mesh_vbo_ != 0) {
        glDeleteBuffers(1, &terrain_mesh_vbo_);
        terrain_mesh_vbo_ = 0;
    }

    if (terrain_mesh_vao_ != 0) {
        glDeleteVertexArrays(1, &terrain_mesh_vao_);
        terrain_mesh_vao_ = 0;
    }

    if (material_overlay_ebo_ != 0) {
        glDeleteBuffers(1, &material_overlay_ebo_);
        material_overlay_ebo_ = 0;
    }

    if (material_overlay_vbo_ != 0) {
        glDeleteBuffers(1, &material_overlay_vbo_);
        material_overlay_vbo_ = 0;
    }

    if (material_overlay_vao_ != 0) {
        glDeleteVertexArrays(1, &material_overlay_vao_);
        material_overlay_vao_ = 0;
    }

    if (ball_vbo_ != 0) {
        glDeleteBuffers(1, &ball_vbo_);
        ball_vbo_ = 0;
    }

    if (ball_vao_ != 0) {
        glDeleteVertexArrays(1, &ball_vao_);
        ball_vao_ = 0;
    }

    flight_path_buffer_.shutdown();

    if (flight_path_vao_ != 0) {
        glDeleteVertexArrays(1, &flight_path_vao_);
        flight_path_vao_ = 0;
    }

    if (cylinder_vbo_ != 0) {
        glDeleteBuffers(1, &cylinder_vbo_);
        cylinder_vbo_ = 0;
    }

    if (cylinder_vao_ != 0) {
        glDeleteVertexArrays(1, &cylinder_vao_);
        cylinder_vao_ = 0;
    }

    if (cone_vbo_ != 0) {
        glDeleteBuffers(1, &cone_vbo_);
        cone_vbo_ = 0;
    }

    if (screen_vbo_ != 0) {
        glDeleteBuffers(1, &screen_vbo_);
        screen_vbo_ = 0;
    }

    if (screen_vao_ != 0) {
        glDeleteVertexArrays(1, &screen_vao_);
        screen_vao_ = 0;
    }

    scene_fbo_.shutdown();
    window_ = nullptr;
}

void renderer::render(const render_data& data, const text_assets& text, frame_profile* profile) {
    if (!window_) {
        return;
    }

    const profile_scope render_timer(profile, profile_stage::render);
    terrain_shader_.set_profile(profile);
    ball_shader_.set_profile(profile);
    crt_shader_.set_profile(profile);
    gpu_timers_.begin_frame(profile != nullptr);

    int screen_width = 0;
    int screen_height = 0;
    SDL_GL_GetDrawableSize(window_, &screen_width, &screen_height);
    screen_width = std::max(1, screen_width);
    screen_height = std::max(1, screen_height);
    if (!ensure_framebuffer_size(screen_width, screen_height)) {
        return;
    }

    glEnable(GL_DEPTH_TEST);

    scene_fbo_.bind();
    glViewport(0, 0, target_width_, target_height_);
    glClearColor(0.36f, 0.56f, 0.82f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 proj = glm::perspective(glm::radians(std::max(1.0f, data.camera_fov_degrees)),
                                            static_cast<float>(target_width_) / static_cast<float>(target_height_),
                                            0.1f,
                                            std::max(160.0f, data.course_extent * 2.5f));
    const glm::mat4 view = glm::lookAt(data.camera_position,
                                       data.camera_target,
                                       glm::vec3(0.0f, 1.0f, 0.0f));

    {
        const profile_scope scene_timer(profile, profile_stage::render_scene);
        render_scene(view, proj, data, profile);
    }

    {
        const profile_scope overlay_timer(profile, profile_stage::render_overlay);
        render_overlay(view, proj, data, text, profile);
    }

    framebuffer::bind_default();
    {
        const profile_scope crt_timer(profile, profile_stage::render_crt);
        render_crt(screen_width, screen_height, profile);
    }

    gpu_timers_.collect(profile);
}

bool renderer::init_shaders() {
    const std::string terrain_vert = asset_path("shaders/terrain.vert");
    const std::string terrain_frag = asset_path("shaders/terrain.frag");
    const std::string ball_vert = asset_path("shaders/ball.vert");
    const std::string ball_frag = asset_path("shaders/ball.frag");
    const std::string crt_vert = asset_path("shaders/crt.vert");
    const std::string crt_frag = asset_path("shaders/crt.frag");

    if (!terrain_shader_.load_from_files(terrain_vert.c_str(), terrain_frag.c_str())) {
        return false;
    }

    if (!ball_shader_.load_from_files(ball_vert.c_str(), ball_frag.c_str())) {
        return false;
    }

    if (!crt_shader_.load_from_files(crt_vert.c_str(), crt_frag.c_str())) {
        return false;
    }

    return true;
}

bool renderer::init_geometry() {
    const float ground_vertices[] = {
        -12.0f, 0.0f, -12.0f,
         12.0f, 0.0f, -12.0f,
         12.0f, 0.0f,  12.0f,
        -12.0f, 0.0f, -12.0f,
         12.0f, 0.0f,  12.0f,
        -12.0f, 0.0f,  12.0f
    };

    glGenVertexArrays(1, &ground_vao_);
    glGenBuffers(1, &ground_vbo_);
    glBindVertexArray(ground_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ground_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ground_vertices), ground_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    glGenVertexArrays(1, &terrain_mesh_vao_);
    glGenBuffers(1, &terrain_mesh_vbo_);
    glGenBuffers(1, &terrain_mesh_ebo_);
    glBindVertexArray(terrain_mesh_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrain_mesh_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_mesh_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, color)));
    glBindVertexArray(0);

    glGenVertexArrays(1, &material_overlay_vao_);
    glGenBuffers(1, &material_overlay_vbo_);
    glGenBuffers(1, &material_overlay_ebo_);
    glBindVertexArray(material_overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, material_overlay_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, material_overlay_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, color)));
    glBindVertexArray(0);

    const std::vector<float> ball_vertices = make_sphere_vertices(primitive_sphere_latitude_segments,
                                                                 primitive_sphere_longitude_segments);
    ball_vertex_count_ = static_cast<int>(ball_vertices.size() / 6);

    glGenVertexArrays(1, &ball_vao_);
    glGenBuffers(1, &ball_vbo_);
    glBindVertexArray(ball_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ball_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(ball_vertices.size() * sizeof(float)),
                 ball_vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    // Flight paths are capped at game_tuning::flight_path::max_points (96),
    // so this capacity covers every shot without ever reallocating.
    constexpr std::size_t flight_path_capacity_vertices = 128;
    glGenVertexArrays(1, &flight_path_vao_);
    glBindVertexArray(flight_path_vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    flight_path_buffer_.init(flight_path_capacity_vertices * sizeof(render_terrain_vertex));
    flight_path_vertices_.reserve(flight_path_capacity_vertices);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, color)));
    glBindVertexArray(0);

    const std::vector<float> cylinder_vertices = make_cylinder_vertices(primitive_cylinder_segments);
    cylinder_vertex_count_ = static_cast<int>(cylinder_vertices.size() / 6);

    glGenVertexArrays(1, &cylinder_vao_);
    glGenBuffers(1, &cylinder_vbo_);
    glBindVertexArray(cylinder_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, cylinder_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cylinder_vertices.size() * sizeof(float)),
                 cylinder_vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    // Cone vertices are only ever drawn through tree_renderer's instanced VAO,
    // which describes this buffer itself, so no VAO is created here.
    const std::vector<float> cone_vertices = make_cone_vertices(primitive_cone_segments);
    cone_vertex_count_ = static_cast<int>(cone_vertices.size() / 6);

    glGenBuffers(1, &cone_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, cone_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cone_vertices.size() * sizeof(float)),
                 cone_vertices.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const float screen_vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f
    };

    glGenVertexArrays(1, &screen_vao_);
    glGenBuffers(1, &screen_vbo_);
    glBindVertexArray(screen_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screen_vertices), screen_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    return true;
}

bool renderer::init_framebuffer() {
    return scene_fbo_.init(target_width_, target_height_);
}

bool renderer::ensure_framebuffer_size(const int screen_width, const int screen_height) {
    const float screen_aspect = static_cast<float>(std::max(1, screen_width)) /
        static_cast<float>(std::max(1, screen_height));
    const float reference_pixels = static_cast<float>(reference_low_res_width * reference_low_res_height);
    int next_width = std::max(min_low_res_dimension,
                              static_cast<int>(std::floor(std::sqrt(reference_pixels * screen_aspect) + 0.5f)));
    int next_height = static_cast<int>(std::floor(static_cast<float>(next_width) / screen_aspect + 0.5f));
    if (next_height < min_low_res_dimension) {
        next_height = min_low_res_dimension;
        next_width = static_cast<int>(std::floor(static_cast<float>(next_height) * screen_aspect + 0.5f));
    }

    if (next_width == target_width_ && next_height == target_height_ &&
        scene_fbo_.width() == target_width_ && scene_fbo_.height() == target_height_) {
        return true;
    }

    target_width_ = next_width;
    target_height_ = next_height;
    return init_framebuffer();
}

void renderer::upload_terrain_mesh(const render_data& data, frame_profile* profile) {
    const render_static_mesh* mesh = data.terrain_mesh;
    if (mesh == nullptr) {
        terrain_mesh_index_count_ = 0;
        terrain_mesh_uploaded_ = false;
        uploaded_terrain_revision_ = 0;
        return;
    }
    if (terrain_mesh_uploaded_ && uploaded_terrain_revision_ == mesh->revision) {
        return;
    }
    if (terrain_mesh_vao_ == 0 || terrain_mesh_vbo_ == 0 || terrain_mesh_ebo_ == 0 ||
        mesh->vertices.empty() || mesh->indices.empty()) {
        terrain_mesh_index_count_ = 0;
        terrain_mesh_uploaded_ = true;
        uploaded_terrain_revision_ = mesh->revision;
        return;
    }

    terrain_mesh_index_count_ = static_cast<int>(mesh->indices.size());

    glBindVertexArray(terrain_mesh_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrain_mesh_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->vertices.size() * sizeof(render_terrain_vertex)),
                 mesh->vertices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->vertices.size() * sizeof(render_terrain_vertex));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_mesh_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->indices.size() * sizeof(std::uint32_t)),
                 mesh->indices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->indices.size() * sizeof(std::uint32_t));
    glBindVertexArray(0);
    terrain_mesh_uploaded_ = true;
    uploaded_terrain_revision_ = mesh->revision;
}

void renderer::upload_material_overlay_mesh(const render_data& data, frame_profile* profile) {
    const render_static_mesh* mesh = data.material_overlay_mesh;
    if (mesh == nullptr) {
        material_overlay_index_count_ = 0;
        material_overlay_mesh_uploaded_ = false;
        uploaded_material_overlay_revision_ = 0;
        return;
    }
    if (material_overlay_mesh_uploaded_ && uploaded_material_overlay_revision_ == mesh->revision) {
        return;
    }
    if (material_overlay_vao_ == 0 || material_overlay_vbo_ == 0 || material_overlay_ebo_ == 0 ||
        mesh->vertices.empty() || mesh->indices.empty()) {
        material_overlay_index_count_ = 0;
        material_overlay_mesh_uploaded_ = true;
        uploaded_material_overlay_revision_ = mesh->revision;
        return;
    }

    material_overlay_index_count_ = static_cast<int>(mesh->indices.size());

    glBindVertexArray(material_overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, material_overlay_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->vertices.size() * sizeof(render_terrain_vertex)),
                 mesh->vertices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->vertices.size() * sizeof(render_terrain_vertex));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, material_overlay_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->indices.size() * sizeof(std::uint32_t)),
                 mesh->indices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->indices.size() * sizeof(std::uint32_t));
    glBindVertexArray(0);
    material_overlay_mesh_uploaded_ = true;
    uploaded_material_overlay_revision_ = mesh->revision;
}

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile) {
    upload_terrain_mesh(data, profile);
    upload_material_overlay_mesh(data, profile);

    const float course_scale = std::max(1.0f, data.course_extent / 12.0f);
    const float terrain_min_y = render_mesh_min_y_or_zero(data.terrain_mesh);
    const float background_ground_y = std::min(-0.08f, terrain_min_y - 2.0f);
    const glm::mat4 ground_model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, background_ground_y, 0.0f)),
                                              glm::vec3(course_scale, 1.0f, course_scale));

    gpu_timers_.begin(gpu_profile_stage::terrain);
    terrain_shader_.use();
    terrain_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.80f, 0.42f)));
    set_terrain_draw_state(terrain_shader_, ground_model, view, proj, glm::vec3(0.10f, 0.26f, 0.13f), false);

    glBindVertexArray(ground_vao_);
    draw_arrays(terrain_shader_, GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Frustum culling of the chunked static meshes. The chunk lists were built
    // with the meshes (app::refresh_render_mesh_cache) and describe the index
    // buffer that was just uploaded for this revision.
    const view_frustum frustum = make_view_frustum(proj * view);
    cull_stats_ = renderer_cull_stats{};

    if (terrain_mesh_index_count_ > 0) {
        cull_stats_.terrain = collect_visible_index_ranges(chunks_of(data.terrain_mesh),
                                                           frustum,
                                                           static_cast<std::size_t>(terrain_mesh_index_count_),
                                                           max_terrain_draw_ranges,
                                                           terrain_draw_ranges_);
        if (!terrain_draw_ranges_.empty()) {
            set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), view, proj, glm::vec3(0.18f, 0.42f, 0.18f), true);
            glBindVertexArray(terrain_mesh_vao_);
            draw_index_ranges(terrain_shader_, terrain_draw_ranges_);
            glBindVertexArray(0);
        }
    }

    if (material_overlay_index_count_ > 0) {
        cull_stats_.material_overlay = collect_visible_index_ranges(chunks_of(data.material_overlay_mesh),
                                                                    frustum,
                                                                    static_cast<std::size_t>(material_overlay_index_count_),
                                                                    max_material_overlay_draw_ranges,
                                                                    material_overlay_draw_ranges_);
        if (!material_overlay_draw_ranges_.empty()) {
            set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), view, proj, glm::vec3(1.0f), true);
            glBindVertexArray(material_overlay_vao_);
            draw_index_ranges(terrain_shader_, material_overlay_draw_ranges_);
            glBindVertexArray(0);
        }
    }

    gpu_timers_.end();

    gpu_timers_.begin(gpu_profile_stage::trees);
    cull_stats_.trees_visible = tree_renderer_.draw(render_trees(data),
                                                    data.trees_revision,
                                                    view,
                                                    proj,
                                                    frustum,
                                                    profile);
    gpu_timers_.end();

    const primitive_geometry primitives{
        screen_vao_,
        cylinder_vao_,
        ball_vao_,
        cylinder_vertex_count_,
        ball_vertex_count_
    };
    terrain_shader_.use();
    terrain_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.80f, 0.42f)));

    world_marker_scene markers;
    markers.show_primary_hole_markers = data.show_primary_hole_markers;
    markers.tee_position = data.tee_position;
    markers.pin_position = data.pin_position;
    markers.start_markers = data.start_markers;
    markers.tee_markers = data.tee_markers;
    markers.pin_markers = data.pin_markers;
    markers.cup_radius = data.cup_radius;
    markers.cup_visual_radius_meters = data.cup_visual_radius_meters;
    markers.pin_visual_height_meters = data.pin_visual_height_meters;
    markers.show_aim_indicator = data.show_aim_indicator;
    markers.aim_arc_points = &data.aim_arc_points;
    markers.show_swing_club = data.shot_addressing || data.swing_timing;
    markers.ball_position = data.ball_position;
    markers.ball_visual_radius_meters = data.ball_visual_radius_meters;
    markers.aim_angle = data.aim_angle;
    markers.swing_power = data.swing_power;
    markers.cart_active = data.cart_active;
    markers.camera_position = data.camera_position;
    markers.camera_target = data.camera_target;
    build_world_marker_batch(world_marker_batch_, markers);
    world_marker_renderer_.draw(world_marker_batch_, proj * view, profile);

    // The emote props stay immediate-mode: the smoke puffs alpha-blend, and
    // the batch is opaque-only. They are submitted after the batch so that the
    // blended puffs composite over the opaque scene behind them, which is what
    // the cart (previously drawn immediately before them) gave them too.
    terrain_shader_.use();
    draw_emote_world_model(terrain_shader_, view, proj, data, primitives);
    glBindVertexArray(0);

    const std::vector<glm::vec3>& flight_path_points = flight_path_points_or_empty(data);
    if (data.show_flight_path && flight_path_points.size() > 1 && flight_path_vao_ != 0) {
        // Rebuilt into a member vector so a shot in flight does not allocate
        // every frame, and streamed into the grow-only buffer.
        flight_path_vertices_.clear();
        flight_path_vertices_.reserve(flight_path_points.size());
        for (const glm::vec3& point : flight_path_points) {
            render_terrain_vertex vertex;
            vertex.position = point + glm::vec3(0.0f, 0.02f, 0.0f);
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.color = glm::vec3(1.0f);
            flight_path_vertices_.push_back(vertex);
        }

        glBindVertexArray(flight_path_vao_);
        flight_path_buffer_.upload(flight_path_vertices_.data(),
                                   flight_path_vertices_.size() * sizeof(render_terrain_vertex),
                                   profile);

        terrain_shader_.use();
        const glm::mat4 model(1.0f);
        terrain_shader_.set_mat4("u_model", model);
        terrain_shader_.set_mat4("u_mvp", proj * view * model);
        terrain_shader_.set_vec3("u_color", data.flight_path_color);
        terrain_shader_.set_float("u_alpha", data.flight_path_alpha);
        terrain_shader_.set_int("u_use_vertex_color", 0);
        terrain_shader_.set_vec3("u_light_dir", glm::vec3(0.0f, 1.0f, 0.0f));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(std::max(1.0f, data.flight_path_width));
        draw_arrays(terrain_shader_, GL_LINE_STRIP, 0, static_cast<GLsizei>(flight_path_vertices_.size()));
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }

    const float ball_radius = std::max(0.02f, data.ball_visual_radius_meters);
    const glm::mat4 ball_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                           data.ball_position),
                                            glm::vec3(ball_radius));
    const glm::mat4 ball_mvp = proj * view * ball_model;

    ball_shader_.use();
    ball_shader_.set_mat4("u_mvp", ball_mvp);
    ball_shader_.set_mat4("u_model", ball_model);
    ball_shader_.set_vec3("u_color", glm::vec3(0.9f, 0.9f, 0.9f));
    ball_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.75f, 0.45f)));

    glBindVertexArray(ball_vao_);
    draw_arrays(ball_shader_, GL_TRIANGLES, 0, ball_vertex_count_);
    glBindVertexArray(0);
}

void renderer::render_overlay(const glm::mat4& view,
                              const glm::mat4& proj,
                              const render_data& data,
                              const text_assets& text,
                              frame_profile* profile) {
    gpu_timers_.begin(gpu_profile_stage::overlay);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Every overlay primitive appends to one vertex stream in submission order
    // (per-vertex colour + alpha), so painter's-order blending is preserved
    // and the whole overlay normally goes out in a single draw call. Nothing
    // below touches GL state except the explicit flushes.
    overlay_batch& batch = overlay_pass_.begin(profile);

    if (data.show_course_results) {
        draw_course_results(batch, text, data.scorecard);
        if (data.show_fps) {
            draw_debug_overlay(overlay_pass_, text, data, profile);
        }
        overlay_pass_.flush();
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        gpu_timers_.end();
        return;
    }

    if (data.show_course_map) {
        draw_paper_course_map(overlay_pass_, course_map_fill_cache_, data);
    }

    if (data.show_scorecard) {
        draw_compact_scorecard(batch, text, data.scorecard);
    }

    if (data.show_skills_panel) {
        draw_skills_panel(batch, text, data.skills);
    }

    if (data.show_fps) {
        draw_debug_overlay(overlay_pass_, text, data, profile);
    }

    if (data.show_rangefinder) {
        draw_rangefinder_view(batch, text, view, proj, data);
    }

    draw_cart_hud(batch, text, data);
    draw_club_label(batch, text, data.selected_club_label);

    if (data.show_interact_prompt) {
        draw_interact_prompt(batch);
    }

    draw_xp_drops(batch, text, data.xp_drops);

    draw_controls_overlay(batch, text, data.controls);

    if (data.show_power_meter) {
        draw_power_meter(batch, text, data.swing_power);
    }

    draw_stroke_ticks(batch, data.stroke_count);

    draw_startup_menu(batch, text, data.startup_menu);

    overlay_pass_.flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    gpu_timers_.end();
}

void renderer::render_crt(int screen_width, int screen_height, frame_profile* profile) {
    (void)profile;
    gpu_timers_.begin(gpu_profile_stage::crt);
    glViewport(0, 0, screen_width, screen_height);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);

    crt_shader_.use();
    crt_shader_.set_int("u_scene", 0);
    crt_shader_.set_vec2("u_resolution", glm::vec2(static_cast<float>(screen_width),
                                                    static_cast<float>(screen_height)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_fbo_.color_texture());

    glBindVertexArray(screen_vao_);
    draw_arrays(crt_shader_, GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    gpu_timers_.end();
}
