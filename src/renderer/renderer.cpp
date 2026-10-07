#include "renderer/renderer.h"

#include "renderer/course_map_overlay.h"
#include "renderer/cart_batch.h"
#include "renderer/emote_props.h"
#include "renderer/gl_loader.h"
#include "renderer/hud_overlay.h"
#include "renderer/menu_overlay.h"
#include "renderer/primitive_mesh.h"
#include "renderer/remote_avatar_batch.h"
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
constexpr const char* fence_net_texture_path = "textures/fence_net.bmp";
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
        hole_sign_renderer_.init(path("hole_sign.vert"), path("hole_sign.frag")) &&
        fence_renderer_.init(path("fence.vert"), path("fence.frag"),
                             (std::filesystem::path(asset_root) / fence_net_texture_path).string()) &&
        water_renderer_.init(path("water.vert"), path("water.frag"), path("underwater.vert"), path("underwater.frag"),
                             screen_vao_) &&
        overlay_pass_.init(path("overlay.vert"), path("overlay.frag")) &&
        backdrop_pass_.init(asset_root, path("backdrop.vert"), path("backdrop.frag"), screen_vao_);
    if (!ok) {
        return false;
    }

    const std::optional<rgba_image> grass = load_bmp_file(std::filesystem::path(asset_root) / grass_texture_path);
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
    hole_sign_renderer_.shutdown();
    fence_renderer_.shutdown();
    water_renderer_.shutdown();
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

    const glm::mat4 proj = scene_projection(data.camera_fov_degrees, data.area_extent);
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, glm::vec3(0.0f, 1.0f, 0.0f));

    {
        const profile_scope scene_timer(profile, profile_stage::render_scene);
        render_scene(view, proj, data, profile);
        render_viewmodel(view, data, profile);
        if (data.camera_underwater) {
            water_renderer_.draw_underwater(profile);
        }
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

void renderer::render_loading_screen(const text_assets& text) {
    if (window_ == nullptr) {
        return;
    }
    int screen_width = 0;
    int screen_height = 0;
    SDL_GL_GetDrawableSize(window_, &screen_width, &screen_height);
    screen_width = std::max(1, screen_width);
    screen_height = std::max(1, screen_height);
    if (!ensure_framebuffer_size(screen_width, screen_height)) {
        return;
    }

    scene_fbo_.bind();
    glViewport(0, 0, target_width_, target_height_);
    glClearColor(crt_border_color.r, crt_border_color.g, crt_border_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_loading_screen(overlay_pass_.begin(nullptr, overlay_grid{target_width_, target_height_}), text);
    overlay_pass_.flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    framebuffer::bind_default();
    render_crt(screen_width, screen_height);
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

glm::mat4 renderer::scene_projection(const float fov_degrees, const float area_extent) const {
    return glm::perspective(glm::radians(std::max(1.0f, fov_degrees)),
                            static_cast<float>(target_width_) / static_cast<float>(target_height_), 0.1f,
                            std::max(min_far_plane, area_extent * far_plane_extent_scale));
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
    const scene_haze haze = backdrop_pass_.draw(data.backdrop, data.camera_position, data.area_extent, view, proj);
    terrain_shader_.use();
    terrain_shader_.set_vec3("u_light_dir", light_direction);
    set_scene_haze_uniforms(terrain_shader_, haze);
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
                                                    data.trees_revision, view, proj, haze, frustum, profile);
    hole_sign_renderer_.draw(data.hole_signs, view_proj, light_direction, haze, profile);
    fence_renderer_.draw_posts(data.fences, view_proj, light_direction, haze, profile);
    gpu_timers_.end();

    world_marker_scene markers;
    markers.show_hole = data.show_hole;
    markers.pin_position = data.pin_position;
    markers.tee_boxes = data.tee_boxes;
    markers.pin_markers = &data.pin_markers;
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
    markers.remote_avatars = &data.remote_avatars;
    markers.remote_balls = &data.remote_balls;
    markers.avatar_eye_height = data.avatar_eye_height;
    build_world_marker_batch(world_marker_batch_, markers);
    world_marker_renderer_.draw(world_marker_batch_, view_proj, profile);

    // Other players' emote props blend (smoke puffs), so they go after the
    // opaque markers.
    emote_props_.clear();
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        if (const std::optional<emote_holder> holder = remote_emote_holder(avatar, data.avatar_eye_height)) {
            append_emote_props(emote_props_, *holder);
        }
    }
    draw_emote_props(view_proj);
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
    // Water and nets blend over everything solid, the ball included.
    water_renderer_.draw_surfaces(data.water, view_proj, haze, profile);
    fence_renderer_.draw_nets(data.fences, view_proj, light_direction, haze, profile);
}

// What is pinned to the camera, my cart and my emote props, drawn over the
// scene at the field of view it was posed for (render_data.h).
void renderer::render_viewmodel(const glm::mat4& view, const render_data& data, frame_profile* profile) {
    viewmodel_batch_.clear();
    append_cart_model(viewmodel_batch_, data.cart_active, data.camera_position, data.camera_target);
    emote_props_.clear();
    if (data.smoke_emote_active || data.beer_emote_active) {
        emote_holder mine;
        mine.eye = data.camera_position;
        mine.target = data.camera_target;
        mine.smoke_elapsed = data.smoke_emote_active ? std::optional<float>(data.smoke_emote_elapsed) : std::nullopt;
        mine.drink_elapsed = data.beer_emote_active ? std::optional<float>(data.beer_emote_elapsed) : std::nullopt;
        append_emote_props(emote_props_, mine);
    }
    if (viewmodel_batch_.empty() && emote_props_.empty()) {
        return;
    }
    // Never cut into by the world it sits in.
    glClear(GL_DEPTH_BUFFER_BIT);
    const glm::mat4 view_proj = scene_projection(data.viewmodel_fov_degrees, data.area_extent) * view;
    world_marker_renderer_.draw(viewmodel_batch_, view_proj, profile);
    draw_emote_props(view_proj);
}

// The emote props collected this frame: the opaque pieces, then the smoke
// blended over them.
void renderer::draw_emote_props(const glm::mat4& view_proj) {
    if (emote_props_.empty()) {
        return;
    }
    terrain_shader_.use();
    const auto draw = [&](const bool blended) {
        for (const emote_prop& prop : emote_props_) {
            if ((prop.alpha < 1.0f) != blended) {
                continue;
            }
            const primitive_buffers& mesh = prop.mesh == emote_prop_mesh::cylinder ? cylinder_
                                            : prop.mesh == emote_prop_mesh::sphere ? ball_
                                                                                   : panel_;
            set_terrain_draw_state(terrain_shader_, prop.model, view_proj, prop.color, prop.alpha, false);
            glBindVertexArray(mesh.vao);
            draw_arrays(terrain_shader_, GL_TRIANGLES, 0, mesh.vertex_count);
        }
    };
    draw(false);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw(true);
    glDisable(GL_BLEND);
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

void renderer::render_course_map(const render_data& data, const text_assets& text) {
    const course_map_layout layout = make_course_map_layout(data, overlay_grid{target_width_, target_height_});
    draw_course_map_paper(overlay_pass_.batch(), layout);
    update_course_map_fill_cache(course_map_fill_cache_, layout, data.terrain_mesh);
    // The fill comes from its own retained buffer, between the paper and the marks.
    overlay_pass_.draw_retained(course_map_fill_cache_.fill.vertices, course_map_fill_cache_.revision);
    draw_course_map_marks(overlay_pass_.batch(), text, layout, data);
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
        draw_course_results(batch, text, data.scorecard, data.group_scorecard);
    } else {
        if (data.show_course_map) {
            render_course_map(data, text);
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
