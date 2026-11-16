#include "renderer/grass_renderer.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "renderer/frustum.h"
#include "renderer/gl_loader.h"
#include "renderer/shadow_map_pass.h"
#include "renderer/terrain_renderer.h"

namespace {
constexpr GLuint blade_location = 0;
constexpr int map_unit = 8;
constexpr int blades_per_tuft = 5;
// The map's texels where no ground was drawn: far below anything.
constexpr float no_ground = -1000.0f;
// Blocks drawn at once: two corners to each of the blade shader's u_blocks.
constexpr int blocks_per_draw = 128;
// Where a ring's tufts thin out, as shares of its reach: the next ring's
// thicken in over the same span.
constexpr float thin_from_share = 0.55f;
// The blades' tops over the ground, over their height, with heavy rough's
// and the wind's lean to spare.
constexpr float blade_top_share = 1.5f;
}

bool grass_renderer::init(const std::string& map_vertex, const std::string& map_fragment, const std::string& blade_vertex,
                          const std::string& blade_fragment) {
    shutdown();
    if (!map_shader_.load_from_files(map_vertex, map_fragment) || !blade_shader_.load_from_files(blade_vertex, blade_fragment)) {
        shutdown();
        return false;
    }
    for (ring_map& ring : rings_) {
        if (!ring.map.init(grass_map_texels, grass_map_texels,
                           framebuffer_format{framebuffer_color::exact, framebuffer_depth::drawn, false, false})) {
            shutdown();
            return false;
        }
    }
    const std::vector<glm::vec3> tuft = grass_tuft_vertices(blades_per_tuft);
    vertex_count_ = static_cast<int>(tuft.size());
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0) {
        SDL_Log("Grass renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tuft.size() * sizeof(glm::vec3)), tuft.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(blade_location);
    glVertexAttribPointer(blade_location, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glGenVertexArrays(1, &tee_vao_);
    glGenBuffers(1, &tee_vbo_);
    if (tee_vao_ == 0 || tee_vbo_ == 0) {
        SDL_Log("Grass renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }
    glBindVertexArray(tee_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tee_vbo_);
    describe_render_terrain_vertex();
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void grass_renderer::shutdown() {
    for (unsigned int* buffer : {&vbo_, &tee_vbo_}) {
        if (*buffer != 0) {
            glDeleteBuffers(1, buffer);
            *buffer = 0;
        }
    }
    for (unsigned int* array : {&vao_, &tee_vao_}) {
        if (*array != 0) {
            glDeleteVertexArrays(1, array);
            *array = 0;
        }
    }
    for (ring_map& ring : rings_) {
        ring.map.shutdown();
        ring.mapped = false;
    }
    map_shader_.shutdown();
    blade_shader_.shutdown();
    vertex_count_ = 0;
}

void grass_renderer::map_ground(const grass_frame& frame, ring_map& ring, const float reach, terrain_renderer& ground,
                                frame_profile* profile) {
    ring.window = grass_window_around(frame.eye, reach);
    GLint bound = 0;
    GLint viewport[4] = {};
    GLfloat clear[4] = {};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    ring.map.bind();
    glViewport(0, 0, grass_map_texels, grass_map_texels);
    glClearColor(no_ground, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const glm::mat4 view_proj = grass_window_view_proj(ring.window);
    map_shader_.set_profile(profile);
    map_shader_.use();
    map_shader_.set_float("u_base", ring.window.base);
    // The overlay lies on the ground: where they meet, it wins (a bunker or a
    // path over rough), and the tee boxes over both.
    ground.draw(map_shader_, frame.terrain, frame.material_overlay, view_proj, make_view_frustum(view_proj),
                overlay_depth::over_all, profile);
    const std::vector<render_terrain_vertex> tees =
        frame.tee_boxes != nullptr ? grass_bare_tee_tops(*frame.tee_boxes) : std::vector<render_terrain_vertex>{};
    if (!tees.empty()) {
        glBindVertexArray(tee_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, tee_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tees.size() * sizeof(render_terrain_vertex)), tees.data(),
                     GL_STREAM_DRAW);
        map_shader_.set_mat4("u_mvp", view_proj);
        const GLboolean culled = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        glDepthFunc(GL_ALWAYS);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tees.size()));
        glDepthFunc(GL_LESS);
        if (culled == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        }
        record_draw_call(profile);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    ring.revision = frame.terrain->revision;
    ring.mapped = true;
}

void grass_renderer::draw(const grass_frame& frame, const grass_style& style, terrain_renderer& ground,
                          const shadow_map_pass& shadows, const int shadow_unit, gl_timer_pool& timers,
                          frame_profile* profile) {
    if (frame.terrain == nullptr || vao_ == 0 || style.blade_height_meters <= 0.0f) {
        return;
    }
    timers.begin(gpu_profile_stage::grass);
    const int rings = std::clamp(static_cast<int>(std::lround(style.rings)), 1, grass_max_rings);
    const view_frustum frustum = make_view_frustum(frame.view_proj);
    const std::vector<const render_static_mesh*> ground_meshes{frame.terrain, frame.material_overlay};
    const float top = blade_top_share * style.blade_height_meters * std::max(1.0f, style.heavy_blade_share);
    for (int k = 0; k < rings; ++k) {
        ring_map& ring = rings_[static_cast<std::size_t>(k)];
        const float scale = std::ldexp(1.0f, k);
        const float reach = style.reach_meters * scale;
        if (!ring.mapped || ring.revision != frame.terrain->revision || !grass_window_serves(ring.window, frame.eye, reach)) {
            map_ground(frame, ring, reach, ground, profile);
        }
        const grass_grid grid = grass_grid_around(frame.eye, reach, style.tuft_spacing_meters * scale);
        // Thinning out from thin_from_share of its reach to its edge, and in
        // where the ring inside thins out (the first never thins in).
        const glm::vec4 thinning = k == 0 ? glm::vec4(-2.0f, -1.0f, thin_from_share * reach, reach)
                                          : glm::vec4(0.5f * thin_from_share * reach, 0.5f * reach, thin_from_share * reach, reach);
        const std::vector<glm::vec2> blocks =
            grass_visible_blocks(grid, frustum, ground_meshes, top, frame.eye, std::max(thinning.x, 0.0f));
        if (blocks.empty()) {
            continue;
        }
        blade_shader_.set_profile(profile);
        blade_shader_.use();
        blade_shader_.set_mat4("u_view_proj", frame.view_proj);
        blade_shader_.set_vec4("u_window", glm::vec4(ring.window.min, ring.window.size, ring.window.base));
        blade_shader_.set_vec2("u_grid", glm::vec2(grid.spacing, static_cast<float>(k)));
        blade_shader_.set_vec4("u_thinning", thinning);
        blade_shader_.set_vec4("u_blade", glm::vec4(style.blade_height_meters, style.blade_width_meters * scale, 0.0f,
                                                    style.blade_sway));
        blade_shader_.set_float("u_heavy_share", style.heavy_blade_share);
        blade_shader_.set_vec3("u_rough_color", frame.rough_color);
        blade_shader_.set_vec2("u_shades", glm::vec2(style.base_shade, style.tip_shade));
        blade_shader_.set_vec2("u_wind", frame.wind);
        blade_shader_.set_float("u_time", frame.clock_seconds);
        blade_shader_.set_int("u_field", map_unit);
        glActiveTexture(GL_TEXTURE0 + map_unit);
        glBindTexture(GL_TEXTURE_2D, ring.map.color_texture());
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
