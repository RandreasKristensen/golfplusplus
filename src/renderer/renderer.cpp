#include "renderer/renderer.h"

#include "renderer/camera_local.h"
#include "renderer/gl_loader.h"
#include "renderer/hud_overlay.h"
#include "renderer/menu_overlay.h"
#include "renderer/primitive_mesh.h"
#include "renderer/scorecard_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
// The scene is drawn at roughly this many pixels (aspect ratio follows the
// window), then upscaled with nearest filtering for the chunky look.
constexpr int reference_low_res_width = 640;
constexpr int reference_low_res_height = 360;
constexpr int min_low_res_dimension = 120;

// Frustum-culled chunk ranges are merged into at most this many draws per
// mesh; culled chunks inside a merged span are drawn rather than costing
// another draw call.
constexpr std::size_t max_terrain_draw_ranges = 4;
constexpr std::size_t max_material_overlay_draw_ranges = 2;
constexpr std::size_t initial_flight_path_vertices = 128;

const glm::vec3 sky_color(0.36f, 0.56f, 0.82f);
const glm::vec3 crt_border_color(0.02f, 0.02f, 0.03f);
const glm::vec3 light_direction = glm::normalize(glm::vec3(-0.35f, 0.80f, 0.42f));
const glm::vec3 backdrop_ground_color(0.10f, 0.26f, 0.13f);
const glm::vec3 ball_color(0.9f, 0.9f, 0.9f);
// The backdrop ground sits this far under the lowest terrain.
constexpr float backdrop_ground_drop = 2.0f;
constexpr float flight_path_lift = 0.02f;
// The drawn zone shapes sit a few centimetres over the ground; this pulls them
// further forward in depth so far-off ones never flicker under it.
constexpr float material_overlay_offset_factor = -1.0f;
constexpr float material_overlay_offset_units = -2.0f;
constexpr int grass_texture_unit = 1;
constexpr const char* grass_texture_path = "textures/rough_grass.bmp";
constexpr float min_far_plane = 160.0f;
constexpr float far_plane_extent_scale = 2.5f;

std::string shader_path(const std::string& asset_root, const char* name) {
    return (std::filesystem::path(asset_root) / "shaders" / name).string();
}

// Draw submissions go through these so draw calls are counted in the profile.
void draw_arrays(const shader_program& shader, const GLenum mode, const GLint first, const GLsizei count) {
    glDrawArrays(mode, first, count);
    record_draw_call(shader.profile());
}

void draw_index_ranges(const shader_program& shader, const std::vector<render_index_range>& ranges) {
    for (const render_index_range& range : ranges) {
        const std::uintptr_t offset = static_cast<std::uintptr_t>(range.first_index) * sizeof(std::uint32_t);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(range.index_count), GL_UNSIGNED_INT, reinterpret_cast<void*>(offset));
        record_draw_call(shader.profile());
    }
}

void describe_render_terrain_vertex() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, rough)));
}

void delete_buffer(unsigned int& buffer) {
    if (buffer != 0) {
        glDeleteBuffers(1, &buffer);
        buffer = 0;
    }
}

void delete_vertex_array(unsigned int& vao) {
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

// Flat (unlit) or vertex-coloured (lit) draw with the terrain shader.
void set_terrain_draw_state(const shader_program& shader,
                            const glm::mat4& model,
                            const glm::mat4& view_proj,
                            const glm::vec3& color,
                            const float alpha,
                            const bool use_vertex_color) {
    shader.set_mat4("u_model", model);
    shader.set_mat4("u_mvp", view_proj * model);
    shader.set_vec3("u_color", color);
    shader.set_float("u_alpha", alpha);
    shader.set_int("u_use_vertex_color", use_vertex_color ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Course map (needs the retained overlay buffer, so it lives here)
// ---------------------------------------------------------------------------

const glm::vec3 map_paper(0.76f, 0.72f, 0.55f);
const glm::vec3 map_paper_shadow(0.14f, 0.12f, 0.09f);
const glm::vec3 map_fold(0.42f, 0.35f, 0.24f);

void expand_map_bounds(const glm::vec3& position, glm::vec2& min_point, glm::vec2& max_point) {
    min_point = glm::min(min_point, glm::vec2(position.x, position.z));
    max_point = glm::max(max_point, glm::vec2(position.x, position.z));
}

course_map_layout make_course_map_layout(const render_data& data) {
    glm::vec2 min_point(data.player_position.x, data.player_position.z);
    glm::vec2 max_point = min_point;
    if (data.show_hole) {
        expand_map_bounds(data.tee_position, min_point, max_point);
        expand_map_bounds(data.pin_position, min_point, max_point);
        expand_map_bounds(data.ball_position, min_point, max_point);
    }
    if (data.terrain_mesh != nullptr && data.terrain_mesh->bounds.valid) {
        expand_map_bounds(data.terrain_mesh->bounds.min, min_point, max_point);
        expand_map_bounds(data.terrain_mesh->bounds.max, min_point, max_point);
    }

    const glm::vec2 size = max_point - min_point;
    const float span = std::max({1.0f, size.x, size.y});

    course_map_layout layout;
    layout.world_center = glm::vec3((min_point.x + max_point.x) * 0.5f, 0.0f, (min_point.y + max_point.y) * 0.5f);
    layout.scale = std::min(layout.half_size.x, layout.half_size.y) * 1.72f / span;
    return layout;
}

void draw_map_marker(overlay_batch& batch, const glm::vec2 position, const glm::vec3 color, const float radius) {
    draw_overlay_quad(batch, position, glm::vec2(radius), glm::vec3(0.04f, 0.035f, 0.025f), 0.55f);
    draw_overlay_quad(batch, position, glm::vec2(radius * 0.64f), color, 1.0f);
}

void draw_map_paper(overlay_batch& batch, const course_map_layout& layout) {
    draw_overlay_quad(batch, layout.center + glm::vec2(0.035f, -0.035f), layout.half_size, map_paper_shadow, 0.42f);
    draw_overlay_quad(batch, layout.center, layout.half_size, map_paper, 0.96f);
    draw_overlay_quad(batch, layout.center, glm::vec2(layout.half_size.x, 0.006f), map_fold, 0.18f);
    draw_overlay_quad(batch, layout.center, glm::vec2(0.006f, layout.half_size.y), map_fold, 0.18f);
    for (const float side : {1.0f, -1.0f}) {
        draw_overlay_quad(batch, layout.center + glm::vec2(0.0f, side * layout.half_size.y),
                          glm::vec2(layout.half_size.x, 0.012f), map_fold, 0.46f);
        draw_overlay_quad(batch, layout.center + glm::vec2(side * layout.half_size.x, 0.0f),
                          glm::vec2(0.012f, layout.half_size.y), map_fold, 0.46f);
    }
}

void draw_map_markers(overlay_batch& batch, const course_map_layout& layout, const render_data& data) {
    if (data.trees != nullptr) {
        for (const tree_body& tree : *data.trees) {
            const float radius = std::clamp(tree.shape.leaf_radius * layout.scale, 0.012f, 0.028f);
            draw_map_marker(batch, map_point(layout, tree.base), glm::vec3(0.08f, 0.24f, 0.11f), radius);
        }
    }
    if (data.show_hole) {
        draw_map_marker(batch, map_point(layout, data.tee_position), glm::vec3(0.34f, 0.21f, 0.12f), 0.023f);
        draw_map_marker(batch, map_point(layout, data.ball_position), glm::vec3(0.94f, 0.93f, 0.82f), 0.020f);
    }
    draw_map_marker(batch, map_point(layout, data.player_position), glm::vec3(0.22f, 0.46f, 0.72f), 0.024f);
    if (data.show_hole) {
        const glm::vec2 pin = map_point(layout, data.pin_position);
        draw_overlay_quad(batch, pin + glm::vec2(0.0f, 0.028f), glm::vec2(0.004f, 0.042f), glm::vec3(0.06f, 0.04f, 0.025f), 0.92f);
        draw_overlay_quad(batch, pin + glm::vec2(0.022f, 0.055f), glm::vec2(0.028f, 0.018f), glm::vec3(0.76f, 0.17f, 0.12f), 0.96f);
        draw_map_marker(batch, pin, glm::vec3(0.94f, 0.78f, 0.22f), 0.018f);
    }
}
}

bool renderer::init(SDL_Window* window, const std::string& asset_root) {
    window_ = window;
    if (window_ == nullptr) {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    const auto path = [&asset_root](const char* name) { return shader_path(asset_root, name); };
    const bool ok = ensure_framebuffer_size(reference_low_res_width, reference_low_res_height) &&
        terrain_shader_.load_from_files(path("terrain.vert"), path("terrain.frag")) &&
        ball_shader_.load_from_files(path("ball.vert"), path("ball.frag")) &&
        crt_shader_.load_from_files(path("crt.vert"), path("crt.frag")) &&
        init_geometry() &&
        tree_renderer_.init(path("tree_instanced.vert"),
                            path("world_marker.frag"),
                            tree_renderer::mesh_source{cylinder_.vbo, cylinder_.vertex_count},
                            tree_renderer::mesh_source{cone_.vbo, cone_.vertex_count}) &&
        world_marker_renderer_.init(path("world_marker.vert"), path("world_marker.frag")) &&
        overlay_pass_.init(path("overlay.vert"), path("overlay.frag")) &&
        backdrop_pass_.init(asset_root, path("backdrop.vert"), path("backdrop.frag"), screen_vao_);
    if (!ok) {
        return false;
    }

    const std::optional<rgb_image> grass = load_bmp_file(std::filesystem::path(asset_root) / grass_texture_path);
    if (!grass || !grass_texture_.upload(*grass, texture_sampling::tiled_detail)) {
        SDL_Log("%s is missing or not an uncompressed 24/32-bit BMP", grass_texture_path);
        return false;
    }
    terrain_shader_.use();
    terrain_shader_.set_int("u_grass", grass_texture_unit);

    gpu_timers_.init();
    return true;
}

void renderer::shutdown() {
    gpu_timers_.shutdown();
    tree_renderer_.shutdown();
    world_marker_renderer_.shutdown();
    overlay_pass_.shutdown();
    backdrop_pass_.shutdown();
    grass_texture_.shutdown();
    terrain_shader_.shutdown();
    ball_shader_.shutdown();
    crt_shader_.shutdown();
    flight_path_buffer_.shutdown();
    delete_vertex_array(flight_path_vao_);

    for (static_mesh_buffers* buffers : {&terrain_, &material_overlay_}) {
        delete_buffer(buffers->ebo);
        delete_buffer(buffers->vbo);
        delete_vertex_array(buffers->vao);
        *buffers = static_mesh_buffers{};
    }
    for (primitive_buffers* buffers : {&ground_, &ball_, &cylinder_, &cone_, &panel_}) {
        delete_buffer(buffers->vbo);
        delete_vertex_array(buffers->vao);
        *buffers = primitive_buffers{};
    }
    delete_buffer(screen_vbo_);
    delete_vertex_array(screen_vao_);

    scene_fbo_.shutdown();
    window_ = nullptr;
}

void renderer::render(const render_data& data, const text_assets& text, frame_profile* profile) {
    if (window_ == nullptr) {
        return;
    }

    const profile_scope render_timer(profile, profile_stage::render);
    terrain_shader_.set_profile(profile);
    ball_shader_.set_profile(profile);
    crt_shader_.set_profile(profile);
    backdrop_pass_.set_profile(profile);
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
    glClearColor(sky_color.r, sky_color.g, sky_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 proj = glm::perspective(glm::radians(std::max(1.0f, data.camera_fov_degrees)),
                                            static_cast<float>(target_width_) / static_cast<float>(target_height_),
                                            0.1f,
                                            std::max(min_far_plane, data.area_extent * far_plane_extent_scale));
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, glm::vec3(0.0f, 1.0f, 0.0f));

    {
        const profile_scope scene_timer(profile, profile_stage::render_scene);
        render_scene(view, proj, data, profile);
    }
    {
        const profile_scope overlay_timer(profile, profile_stage::render_overlay);
        render_overlay(proj * view, data, text, profile);
    }

    framebuffer::bind_default();
    {
        const profile_scope crt_timer(profile, profile_stage::render_crt);
        render_crt(screen_width, screen_height);
    }

    gpu_timers_.collect(profile);
}

bool renderer::init_geometry() {
    const auto make_primitive = [](primitive_buffers& buffers, const std::vector<float>& vertices, const bool with_vao) {
        buffers.vertex_count = static_cast<int>(vertices.size() / 6);
        if (with_vao) {
            glGenVertexArrays(1, &buffers.vao);
            glBindVertexArray(buffers.vao);
        }
        glGenBuffers(1, &buffers.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
        if (with_vao) {
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
            glBindVertexArray(0);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    };
    make_primitive(ground_, make_xz_quad_vertices(), true);
    make_primitive(ball_, make_sphere_vertices(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments), true);
    make_primitive(cylinder_, make_cylinder_vertices(primitive_cylinder_segments), true);
    make_primitive(panel_, make_xy_quad_vertices(), true);
    // Only drawn instanced by tree_renderer, which describes the buffer itself.
    make_primitive(cone_, make_cone_vertices(primitive_cone_segments), false);

    for (static_mesh_buffers* buffers : {&terrain_, &material_overlay_}) {
        glGenVertexArrays(1, &buffers->vao);
        glGenBuffers(1, &buffers->vbo);
        glGenBuffers(1, &buffers->ebo);
        glBindVertexArray(buffers->vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffers->vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers->ebo);
        describe_render_terrain_vertex();
        glBindVertexArray(0);
    }

    glGenVertexArrays(1, &flight_path_vao_);
    glBindVertexArray(flight_path_vao_);
    // init() leaves the buffer bound for the attribute description.
    flight_path_buffer_.init(initial_flight_path_vertices * sizeof(render_terrain_vertex));
    describe_render_terrain_vertex();
    glBindVertexArray(0);

    // Full-screen quad: position.xyz, uv.
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

bool renderer::ensure_framebuffer_size(const int screen_width, const int screen_height) {
    const float aspect = static_cast<float>(std::max(1, screen_width)) / static_cast<float>(std::max(1, screen_height));
    const float reference_pixels = static_cast<float>(reference_low_res_width * reference_low_res_height);
    int width = std::max(min_low_res_dimension, static_cast<int>(std::lround(std::sqrt(reference_pixels * aspect))));
    int height = static_cast<int>(std::lround(static_cast<float>(width) / aspect));
    if (height < min_low_res_dimension) {
        height = min_low_res_dimension;
        width = static_cast<int>(std::lround(static_cast<float>(height) * aspect));
    }

    if (width == target_width_ && height == target_height_ && scene_fbo_.width() == width && scene_fbo_.height() == height) {
        return true;
    }
    target_width_ = width;
    target_height_ = height;
    return scene_fbo_.init(target_width_, target_height_);
}

void renderer::upload_static_mesh(static_mesh_buffers& buffers, const render_static_mesh* mesh, frame_profile* profile) {
    if (mesh == nullptr) {
        buffers.index_count = 0;
        buffers.uploaded = false;
        return;
    }
    if (buffers.uploaded && buffers.uploaded_revision == mesh->revision) {
        return;
    }

    buffers.index_count = static_cast<int>(mesh->indices.size());
    buffers.uploaded = true;
    buffers.uploaded_revision = mesh->revision;
    if (mesh->vertices.empty() || mesh->indices.empty()) {
        buffers.index_count = 0;
        return;
    }

    const std::size_t vertex_bytes = mesh->vertices.size() * sizeof(render_terrain_vertex);
    const std::size_t index_bytes = mesh->indices.size() * sizeof(std::uint32_t);
    glBindVertexArray(buffers.vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_bytes), mesh->vertices.data(), GL_STATIC_DRAW);
    record_buffer_upload(profile, vertex_bytes);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_bytes), mesh->indices.data(), GL_STATIC_DRAW);
    record_buffer_upload(profile, index_bytes);
    glBindVertexArray(0);
}

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile) {
    upload_static_mesh(terrain_, data.terrain_mesh, profile);
    upload_static_mesh(material_overlay_, data.material_overlay_mesh, profile);
    const glm::mat4 view_proj = proj * view;

    gpu_timers_.begin(gpu_profile_stage::terrain);
    backdrop_pass_.draw(data.backdrop_image != nullptr ? *data.backdrop_image : std::string(), view, proj);
    terrain_shader_.use();
    terrain_shader_.set_vec3("u_light_dir", light_direction);
    grass_texture_.bind(grass_texture_unit);

    // Backdrop ground under the whole area so the horizon is never empty.
    const float lowest_terrain = data.terrain_mesh != nullptr && data.terrain_mesh->bounds.valid
        ? data.terrain_mesh->bounds.min.y
        : data.area_center.y;
    const float ground_extent = std::max(1.0f, data.area_extent * 2.0f);
    const glm::mat4 ground_model = glm::scale(
        glm::translate(glm::mat4(1.0f), glm::vec3(data.area_center.x, lowest_terrain - backdrop_ground_drop, data.area_center.z)),
        glm::vec3(ground_extent, 1.0f, ground_extent));
    set_terrain_draw_state(terrain_shader_, ground_model, view_proj, backdrop_ground_color, 1.0f, false);
    glBindVertexArray(ground_.vao);
    draw_arrays(terrain_shader_, GL_TRIANGLES, 0, ground_.vertex_count);

    // The chunk lists were built with the meshes and describe the index buffer
    // uploaded for this revision.
    const view_frustum frustum = make_view_frustum(view_proj);
    cull_stats_ = renderer_cull_stats{};
    const struct {
        static_mesh_buffers& buffers;
        const render_static_mesh* mesh;
        std::size_t max_ranges;
        std::vector<render_index_range>& ranges;
        render_chunk_cull_stats& stats;
        bool pulled_forward;  // drawn over the ground it lies on
    } meshes[] = {
        {terrain_, data.terrain_mesh, max_terrain_draw_ranges, terrain_draw_ranges_, cull_stats_.terrain, false},
        {material_overlay_, data.material_overlay_mesh, max_material_overlay_draw_ranges, material_overlay_draw_ranges_,
         cull_stats_.material_overlay, true},
    };
    for (const auto& mesh : meshes) {
        if (mesh.buffers.index_count <= 0 || mesh.mesh == nullptr) {
            continue;
        }
        mesh.stats = collect_visible_index_ranges(mesh.mesh->chunks, frustum, static_cast<std::size_t>(mesh.buffers.index_count),
                                                  mesh.max_ranges, mesh.ranges);
        if (!mesh.ranges.empty()) {
            set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), view_proj, glm::vec3(1.0f), 1.0f, true);
            glBindVertexArray(mesh.buffers.vao);
            if (mesh.pulled_forward) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(material_overlay_offset_factor, material_overlay_offset_units);
            }
            draw_index_ranges(terrain_shader_, mesh.ranges);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }
    glBindVertexArray(0);
    gpu_timers_.end();

    gpu_timers_.begin(gpu_profile_stage::trees);
    cull_stats_.trees_visible = tree_renderer_.draw(data.trees != nullptr ? *data.trees : std::vector<tree_body>{},
                                                    data.trees_revision, view, proj, frustum, profile);
    gpu_timers_.end();

    world_marker_scene markers;
    markers.show_hole = data.show_hole;
    markers.tee_position = data.tee_position;
    markers.pin_position = data.pin_position;
    markers.start_markers = &data.start_markers;
    markers.tee_markers = data.hub_tee_markers;
    markers.pin_markers = data.hub_pin_markers;
    markers.collectible_markers = &data.collectible_markers;
    markers.cup_radius_meters = data.cup_radius_meters;
    markers.pin_visual_height_meters = data.pin_visual_height_meters;
    markers.show_aim_indicator = data.show_aim_indicator;
    markers.aim_arc_points = &data.aim_arc_points;
    markers.show_swing_club = data.show_swing_club;
    markers.ball_position = data.ball_position;
    markers.ball_visual_radius_meters = data.ball_visual_radius_meters;
    markers.aim_angle = data.aim_angle;
    markers.swing_power = data.swing_power;
    markers.cart_active = data.cart_active;
    markers.camera_position = data.camera_position;
    markers.camera_target = data.camera_target;
    markers.remote_avatars = &data.remote_avatars;
    markers.remote_balls = &data.remote_balls;
    markers.avatar_eye_height = data.avatar_eye_height;
    build_world_marker_batch(world_marker_batch_, markers);
    world_marker_renderer_.draw(world_marker_batch_, view_proj, profile);

    // Emote props blend (smoke puffs), so they go after the opaque markers.
    render_emotes(view, proj, data);
    render_flight_path(view, proj, data, profile);

    if (data.show_ball) {
        const glm::mat4 ball_model = glm::scale(glm::translate(glm::mat4(1.0f), data.ball_position),
                                                glm::vec3(data.ball_visual_radius_meters));
        ball_shader_.use();
        ball_shader_.set_mat4("u_mvp", view_proj * ball_model);
        ball_shader_.set_mat4("u_model", ball_model);
        ball_shader_.set_vec3("u_color", ball_color);
        ball_shader_.set_vec3("u_light_dir", light_direction);
        glBindVertexArray(ball_.vao);
        draw_arrays(ball_shader_, GL_TRIANGLES, 0, ball_.vertex_count);
        glBindVertexArray(0);
    }
}

void renderer::render_emotes(const glm::mat4& view, const glm::mat4& proj, const render_data& data) {
    if (!data.smoke_emote_active && !data.beer_emote_active) {
        return;
    }
    const glm::mat4 view_proj = proj * view;
    terrain_shader_.use();
    const auto draw = [&](const primitive_buffers& mesh, const glm::mat4& model, const glm::vec3& color, const float alpha) {
        set_terrain_draw_state(terrain_shader_, model, view_proj, color, alpha, false);
        glBindVertexArray(mesh.vao);
        draw_arrays(terrain_shader_, GL_TRIANGLES, 0, mesh.vertex_count);
    };
    const glm::vec3& eye = data.camera_position;
    const glm::vec3& target = data.camera_target;

    if (data.smoke_emote_active) {
        const float t = std::max(0.0f, data.smoke_emote_elapsed);
        const float ember = 0.55f + 0.45f * std::sin(t * 24.0f);
        const glm::vec3 cigarette_base(0.08f, -0.42f, 0.78f);
        const glm::vec3 cigarette_tip(0.30f, -0.47f, 1.12f);
        draw(cylinder_, local_segment_model(eye, target, cigarette_base, cigarette_tip, 0.026f), glm::vec3(0.88f, 0.82f, 0.62f), 1.0f);
        draw(ball_, local_sphere_model(eye, target, cigarette_tip, 0.046f), glm::vec3(0.95f, 0.20f + ember * 0.20f, 0.10f), 1.0f);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int i = 0; i < 5; ++i) {
            const float f = static_cast<float>(i);
            const float rise = std::fmod(t * 0.55f + f * 0.18f, 0.90f);
            const float sway = std::sin(t * 4.0f + f * 1.7f) * 0.055f;
            const glm::vec3 puff = cigarette_tip + glm::vec3(sway + f * 0.012f, 0.10f + rise, -rise * 0.10f);
            draw(ball_, local_sphere_model(eye, target, puff, 0.065f + rise * 0.075f), glm::vec3(0.78f, 0.80f, 0.78f),
                 std::clamp(0.48f - rise * 0.42f, 0.04f, 0.42f));
        }
        glDisable(GL_BLEND);
    }

    if (data.beer_emote_active) {
        const float bob = std::sin(data.beer_emote_elapsed * 9.0f) * 0.035f;
        const glm::vec3 can(-0.24f, -0.50f + bob, 0.82f);
        draw(cylinder_,
             local_cylinder_model(eye, target, can, glm::vec3(glm::radians(-8.0f), 0.0f, glm::radians(10.0f)), glm::vec3(0.145f, 0.48f, 0.145f)),
             glm::vec3(0.76f, 0.76f, 0.70f), 1.0f);
        draw(panel_, local_panel_model(eye, target, can + glm::vec3(0.0f, 0.0f, -0.155f), glm::vec3(0.0f), glm::vec2(0.118f, 0.108f)),
             glm::vec3(0.78f, 0.18f, 0.12f), 1.0f);
        draw(panel_,
             local_panel_model(eye, target, can + glm::vec3(0.0f, 0.25f, -0.03f), glm::vec3(glm::radians(90.0f), 0.0f, 0.0f), glm::vec2(0.052f, 0.024f)),
             glm::vec3(0.18f, 0.18f, 0.16f), 1.0f);
    }
    glBindVertexArray(0);
}

// My shot's flight path and other players' shot trails, as line strips.
void renderer::render_flight_path(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile) {
    const auto draw_path = [&](const std::vector<glm::vec3>& points, const float alpha) {
        if (points.size() < 2) {
            return;
        }
        flight_path_vertices_.clear();
        for (const glm::vec3& point : points) {
            render_terrain_vertex vertex;
            vertex.position = point + glm::vec3(0.0f, flight_path_lift, 0.0f);
            flight_path_vertices_.push_back(vertex);
        }
        flight_path_buffer_.upload(flight_path_vertices_.data(), flight_path_vertices_.size() * sizeof(render_terrain_vertex), profile);
        set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), proj * view, data.flight_path_color, alpha, false);
        draw_arrays(terrain_shader_, GL_LINE_STRIP, 0, static_cast<GLsizei>(flight_path_vertices_.size()));
    };
    const bool own_path = data.show_flight_path && data.flight_path_points != nullptr;
    if (!own_path && data.remote_trails.empty()) {
        return;
    }

    glBindVertexArray(flight_path_vao_);
    terrain_shader_.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(std::max(1.0f, data.flight_path_width));
    if (own_path) {
        draw_path(*data.flight_path_points, data.flight_path_alpha);
    }
    for (const render_trail& trail : data.remote_trails) {
        draw_path(trail.points, trail.alpha);
    }
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void renderer::render_course_map(const render_data& data) {
    const course_map_layout layout = make_course_map_layout(data);
    draw_map_paper(overlay_pass_.batch(), layout);
    update_course_map_fill_cache(course_map_fill_cache_, layout, data.terrain_mesh);
    // The fill comes from its own retained buffer, between the paper and the markers.
    overlay_pass_.draw_retained(course_map_fill_cache_.fill.vertices, course_map_fill_cache_.revision);
    draw_map_markers(overlay_pass_.batch(), layout, data);
}

// Flushed as its own draw so its GL cost goes into the debug-overlay counters
// instead of inflating the numbers it displays.
void renderer::render_debug_overlay(const render_data& data, const text_assets& text, frame_profile* profile) {
    overlay_pass_.flush();
    const debug_overlay_cost_mark mark = mark_debug_overlay_cost(profile);
    draw_debug_text(overlay_pass_.batch(), text, data);
    overlay_pass_.flush();
    reclaim_debug_overlay_cost(profile, mark);
}

void renderer::render_overlay(const glm::mat4& view_proj, const render_data& data, const text_assets& text, frame_profile* profile) {
    gpu_timers_.begin(gpu_profile_stage::overlay);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Every primitive appends to one vertex stream in submission order, so
    // painter's-order blending holds and the overlay is normally one draw.
    overlay_batch& batch = overlay_pass_.begin(profile, overlay_grid{target_width_, target_height_});
    if (data.show_course_results) {
        draw_course_results(batch, text, data.scorecard);
    } else {
        if (data.show_course_map) {
            render_course_map(data);
        }
        draw_hud(overlay_pass_.batch(), text, data, view_proj);
        draw_startup_menu(overlay_pass_.batch(), text, data.startup_menu);
    }
    if (data.show_fps) {
        render_debug_overlay(data, text, profile);
    }

    overlay_pass_.flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    gpu_timers_.end();
}

void renderer::render_crt(const int screen_width, const int screen_height) {
    gpu_timers_.begin(gpu_profile_stage::crt);
    glViewport(0, 0, screen_width, screen_height);
    glClearColor(crt_border_color.r, crt_border_color.g, crt_border_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    crt_shader_.use();
    crt_shader_.set_int("u_scene", 0);
    crt_shader_.set_vec2("u_resolution", glm::vec2(static_cast<float>(screen_width), static_cast<float>(screen_height)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_fbo_.color_texture());

    glBindVertexArray(screen_vao_);
    draw_arrays(crt_shader_, GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    gpu_timers_.end();
}
