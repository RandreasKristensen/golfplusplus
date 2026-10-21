#include "renderer/renderer.h"

#include "renderer/course_map_overlay.h"
#include "renderer/emote_props.h"
#include "renderer/gl_loader.h"
#include "renderer/hud_overlay.h"
#include "renderer/menu_overlay.h"
#include "renderer/primitive_mesh.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/scorecard_overlay.h"
#include "renderer/shadow_casters.h"
#include "renderer/vcr_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr std::size_t initial_flight_path_vertices = 128;

// The backdrop ground sits this far under the lowest terrain.
constexpr float backdrop_ground_drop = 2.0f;
constexpr float flight_path_lift = 0.02f;
constexpr int grass_texture_unit = 1;
constexpr int shadow_map_unit = 4;
constexpr const char* grass_texture_path = "world/ground/rough_grass.bmp";
constexpr const char* fence_net_texture_path = "world/props/fence_net.bmp";
constexpr float min_far_plane = 160.0f;
constexpr float far_plane_extent_scale = 2.5f;

std::string shader_path(const std::string& asset_root, const char* name) {
    return (std::filesystem::path(asset_root) / "picture" / "shaders" / name).string();
}

// Draw submissions go through these so draw calls are counted in the profile.
void draw_arrays(const shader_program& shader, const GLenum mode, const GLint first, const GLsizei count) {
    glDrawArrays(mode, first, count);
    record_draw_call(shader.profile());
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
}

bool renderer::init(SDL_Window* window, const std::string& asset_root, const picture_style& picture,
                    const lighting_palette& palette, const prop_set& props) {
    window_ = window;
    picture_style_ = picture;
    colors_ = palette.colors;
    props_ = props;
    if (window_ == nullptr) {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    int screen_width = 0;
    int screen_height = 0;
    SDL_GL_GetDrawableSize(window_, &screen_width, &screen_height);
    const auto path = [&asset_root](const char* name) { return shader_path(asset_root, name); };
    const bool ok = ensure_framebuffer_size(screen_width, screen_height) && lighting_uniforms_.init() &&
        terrain_shader_.load_from_files(path("terrain.vert"), path("terrain.frag")) &&
        ball_shader_.load_from_files(path("ball.vert"), path("ball.frag")) &&
        init_geometry() && terrain_renderer_.init() &&
        camcorder_pass_.init(path(""), screen_vao_, picture.camcorder, picture.tape.fields_per_second) &&
        eyepiece_pass_.init(path("crt.vert"), path("eyepiece.frag"), screen_vao_) &&
        tape_pass_.init(path("crt.vert"), path("tape.frag"), screen_vao_, picture.tape) &&
        tv_pass_.init(path("crt.vert"), path("crt.frag"), screen_vao_, picture.tv) &&
        tree_renderer_.init(path("tree_instanced.vert"),
                            path("world_marker.frag"),
                            tree_renderer::mesh_source{cylinder_.vbo, cylinder_.vertex_count},
                            tree_renderer::mesh_source{cone_.vbo, cone_.vertex_count}) &&
        world_marker_renderer_.init(path("world_marker.vert"), path("world_marker.frag")) &&
        tee_box_renderer_.init(path("tee_box.vert"), path("tee_box.frag")) &&
        blob_shadow_renderer_.init(path("blob_shadow.vert"), path("blob_shadow.frag")) &&
        shadow_map_pass_.init(path("shadow_map.vert"), path("shadow_map.frag"), palette.shadows) &&
        hole_sign_renderer_.init(path("hole_sign.vert"), path("hole_sign.frag")) &&
        fence_renderer_.init(path("fence.vert"), path("fence.frag"),
                             (std::filesystem::path(asset_root) / fence_net_texture_path).string()) &&
        floodlight_renderer_.init(path("floodlight.vert"), path("world_marker.frag")) &&
        water_renderer_.init(path("water.vert"), path("water.frag"), path("underwater.vert"), path("underwater.frag"),
                             screen_vao_) &&
        weather_renderer_.init(path("weather_streak.vert"), path("weather_streak.frag"),
                               std::max(palette.rain.max_drops, palette.wind.max_specks)) &&
        overlay_pass_.init(path("overlay.vert"), path("overlay.frag")) &&
        backdrop_pass_.init(asset_root, path("backdrop.vert"), path("backdrop.frag"), screen_vao_, palette.sky);
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
    terrain_shader_.set_int("u_shadow_map", shadow_map_unit);
    tee_box_renderer_.shader().use();
    tee_box_renderer_.shader().set_int("u_shadow_map", shadow_map_unit);

    gpu_timers_.init();
    return true;
}

void renderer::shutdown() {
    gpu_timers_.shutdown();
    lighting_uniforms_.shutdown();
    tree_renderer_.shutdown();
    world_marker_renderer_.shutdown();
    tee_box_renderer_.shutdown();
    blob_shadow_renderer_.shutdown();
    shadow_map_pass_.shutdown();
    hole_sign_renderer_.shutdown();
    fence_renderer_.shutdown();
    floodlight_renderer_.shutdown();
    water_renderer_.shutdown();
    weather_renderer_.shutdown();
    overlay_pass_.shutdown();
    backdrop_pass_.shutdown();
    grass_texture_.shutdown();
    terrain_shader_.shutdown();
    ball_shader_.shutdown();
    camcorder_pass_.shutdown();
    eyepiece_pass_.shutdown();
    tape_pass_.shutdown();
    tv_pass_.shutdown();
    flight_path_buffer_.shutdown();
    delete_vertex_array(flight_path_vao_);

    terrain_renderer_.shutdown();
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
    camcorder_pass_.set_profile(profile);
    eyepiece_pass_.set_profile(profile);
    tape_pass_.set_profile(profile);
    tv_pass_.set_profile(profile);
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

    const glm::mat4 proj = scene_projection(data.camera_fov_degrees, data.area_extent);
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
    if (!data.vcr.still_frame || !camcorder_pass_.has_frame()) {
        const profile_scope scene_timer(profile, profile_stage::render_scene);
        const std::vector<tree_body> no_trees;
        build_shadow_casters(shadow_casters_, data, colors_, props_);
        shadow_map_pass_.draw(tree_renderer_, data.trees != nullptr ? *data.trees : no_trees, data.trees_revision,
                              floodlight_renderer_, data.floodlights, shadow_casters_,
                              data.lighting.sun_direction, data.lighting.shadows.sun_share, data.camera_position,
                              data.camera_target - data.camera_position, gpu_timers_, profile);
        glEnable(GL_DEPTH_TEST);
        const glm::vec3 sky = data.lighting.sky_zenith;
        if (data.show_rangefinder) {
            gpu_timers_.begin(gpu_profile_stage::zoom);
            eyepiece_pass_.begin_zoom(sky);
            render_scene(view, zoomed_projection(proj, data.rangefinder_eyepiece.magnification), data, profile);
            eyepiece_pass_.end_zoom();
            gpu_timers_.end();
        }
        scene_fbo_.bind();
        glViewport(0, 0, picture_.width, picture_.height);
        glClearColor(sky.r, sky.g, sky.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        render_scene(view, proj, data, profile);
        render_viewmodel(view, data);
        if (data.camera_underwater) {
            water_renderer_.draw_underwater(colors_.ground, profile);
        }
        const unsigned int scene = data.show_rangefinder
            ? eyepiece_pass_.draw(scene_fbo_.color_texture(), data.rangefinder_eyepiece, gpu_timers_)
            : scene_fbo_.color_texture();
        camcorder_pass_.draw(scene, data.lighting, data.clock_seconds, gpu_timers_);
    }
    render_tape(camcorder_pass_.color_texture(), data.clock_seconds, data.tape_tracking);
    {
        const profile_scope overlay_timer(profile, profile_stage::render_overlay);
        render_overlay(proj * view, data, text, profile);
    }
    {
        const profile_scope crt_timer(profile, profile_stage::render_crt);
        gpu_timers_.begin(gpu_profile_stage::crt);
        tv_pass_.draw(tape_pass_.color_texture(), picture_, screen_width, screen_height, picture_style_.tv.border_color);
        gpu_timers_.end();
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
    glViewport(0, 0, picture_.width, picture_.height);
    const glm::vec3 border = picture_style_.tv.border_color;
    glClearColor(border.r, border.g, border.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_tape(scene_fbo_.color_texture(), 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_loading_screen(overlay_pass_.begin(nullptr, overlay_grid{picture_.width, picture_.height}), text);
    overlay_pass_.flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    tv_pass_.draw(tape_pass_.color_texture(), picture_, screen_width, screen_height, border);
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
                            static_cast<float>(picture_.width) / static_cast<float>(picture_.height), 0.1f,
                            std::max(min_far_plane, area_extent * far_plane_extent_scale));
}

bool renderer::ensure_framebuffer_size(const int screen_width, const int screen_height) {
    picture_ = picture_size_for_window(screen_width, screen_height, picture_style_.target_lines);
    const bool scene_ok = (scene_fbo_.width() == picture_.width && scene_fbo_.height() == picture_.height) ||
        scene_fbo_.init(picture_.width, picture_.height, framebuffer_format{framebuffer_color::hdr, true, false, false});
    return scene_ok && camcorder_pass_.ensure_size(picture_.width, picture_.height) &&
        eyepiece_pass_.ensure_size(picture_.width, picture_.height) &&
        tape_pass_.ensure_size(picture_.width, picture_.height);
}

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile) {
    const glm::mat4 view_proj = proj * view;

    gpu_timers_.begin(gpu_profile_stage::terrain);
    const render_sky_weather& weather = data.lighting.weather;
    const scene_haze haze = backdrop_pass_.prepare(data.backdrop, data.camera_position, data.area_extent, data.lighting.haze_tint);
    lighting_uniforms_.upload(data.lighting,
                              weathered_haze(haze, weather_haze{weather.haze_gain, weather.haze_distance_share, weather.haze_grey}),
                              data.lights, profile);
    colors_.glow = data.glow;
    terrain_shader_.use();
    grass_texture_.bind(grass_texture_unit);
    shadow_map_pass_.bind(terrain_shader_, shadow_map_unit);

    // Backdrop ground under the whole area so the horizon is never empty.
    const float lowest_terrain = data.terrain_mesh != nullptr && data.terrain_mesh->bounds.valid
        ? data.terrain_mesh->bounds.min.y
        : data.area_center.y;
    const float ground_extent = std::max(1.0f, data.area_extent * 2.0f);
    const glm::mat4 ground_model = glm::scale(
        glm::translate(glm::mat4(1.0f), glm::vec3(data.area_center.x, lowest_terrain - backdrop_ground_drop, data.area_center.z)),
        glm::vec3(ground_extent, 1.0f, ground_extent));
    set_terrain_draw_state(terrain_shader_, ground_model, view_proj, colors_.ground.backdrop, 1.0f, 0.0f, false);
    glBindVertexArray(ground_.vao);
    draw_arrays(terrain_shader_, GL_TRIANGLES, 0, ground_.vertex_count);

    const view_frustum frustum = make_view_frustum(view_proj);
    const terrain_cull_stats terrain_stats =
        terrain_renderer_.draw(terrain_shader_, data.terrain_mesh, data.material_overlay_mesh, view_proj, frustum, profile);
    cull_stats_ = renderer_cull_stats{terrain_stats.terrain, terrain_stats.material_overlay, false};
    // Under the blobs, which lie on them as on the ground.
    build_tee_box_batch(tee_box_batch_, data.tee_boxes, colors_.markers);
    tee_box_renderer_.shader().use();
    shadow_map_pass_.bind(tee_box_renderer_.shader(), shadow_map_unit);
    tee_box_renderer_.draw(tee_box_batch_, view_proj, profile);
    gpu_timers_.end();

    gpu_timers_.begin(gpu_profile_stage::shadows);
    blob_shadow_renderer_.draw(data.tree_shadows, data.tree_shadows_revision, data.blob_shadows, view_proj, profile);
    gpu_timers_.end();

    gpu_timers_.begin(gpu_profile_stage::trees);
    cull_stats_.trees_visible = tree_renderer_.draw(data.trees != nullptr ? *data.trees : std::vector<tree_body>{},
                                                    data.trees_revision, view, proj, frustum, props_.tree, profile);
    hole_sign_renderer_.draw(data.hole_signs, view_proj, profile);
    fence_renderer_.draw_posts(data.fences, view_proj, profile);
    floodlight_renderer_.draw(data.floodlights, view_proj, profile);
    gpu_timers_.end();

    world_marker_scene markers;
    markers.show_hole = data.show_hole;
    markers.pin_position = data.pin_position;
    markers.pin_markers = &data.pin_markers;
    markers.collectible_markers = &data.collectible_markers;
    markers.cup_radius_meters = data.cup_radius_meters;
    markers.pin_visual_height_meters = data.pin_visual_height_meters;
    markers.flag = flag_pose{data.weather.flag_wind, data.weather.flag_lift, data.weather.flag_phase, data.weather.flag_flap};
    markers.show_aim_indicator = data.show_aim_indicator;
    markers.aim_arc_points = &data.aim_arc_points;
    markers.show_swing_club = data.show_swing_club;
    markers.ball_position = data.ball_position;
    markers.ball_visual_radius_meters = data.ball_visual_radius_meters;
    markers.aim_angle = data.aim_angle;
    markers.swing_power = data.swing_power;
    markers.carts = &data.carts;
    markers.cart_seat_offset = data.cart_seat_offset;
    markers.remote_avatars = &data.remote_avatars;
    markers.remote_balls = &data.remote_balls;
    markers.avatar_eye_height = data.avatar_eye_height;
    build_world_marker_batch(world_marker_batch_, markers, colors_, props_);
    world_marker_renderer_.draw(world_marker_batch_, view_proj, profile);

    // Behind everything solid, before anything blends over it.
    gpu_timers_.begin(gpu_profile_stage::sky);
    backdrop_pass_.draw(view, proj, data.clock_seconds, data.weather.lightning);
    gpu_timers_.end();

    // Other players' emote props blend (smoke puffs), so they go after the
    // opaque markers.
    emote_props_.clear();
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        if (const std::optional<emote_holder> holder = remote_emote_holder(avatar, data.avatar_eye_height)) {
            append_emote_props(emote_props_, *holder, props_, colors_.glow);
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
        ball_shader_.set_vec3("u_color", colors_.markers.ball);
        ball_shader_.set_float("u_glow", colors_.glow.ball);
        glBindVertexArray(ball_.vao);
        draw_arrays(ball_shader_, GL_TRIANGLES, 0, ball_.vertex_count);
        glBindVertexArray(0);
    }
    // Water and nets blend over everything solid, the ball included.
    water_renderer_.draw_surfaces(data.water, view_proj, colors_.ground, profile);
    fence_renderer_.draw_nets(data.fences, view_proj, profile);
    gpu_timers_.begin(gpu_profile_stage::weather);
    weather_renderer_.draw(data.weather, view_proj, profile);
    gpu_timers_.end();
}

// What is pinned to the camera, my emote props, drawn over the scene at the
// field of view it was posed for (render_data.h).
void renderer::render_viewmodel(const glm::mat4& view, const render_data& data) {
    if (!data.smoke_emote_active && !data.beer_emote_active) {
        return;
    }
    emote_props_.clear();
    const emote_holder mine = camera_emote_holder(
        data.camera_position, data.camera_target,
        data.smoke_emote_active ? std::optional<float>(data.smoke_emote_elapsed) : std::nullopt,
        data.beer_emote_active ? std::optional<float>(data.beer_emote_elapsed) : std::nullopt);
    append_emote_props(emote_props_, mine, props_, colors_.glow);
    // Never cut into by the world it sits in.
    glClear(GL_DEPTH_BUFFER_BIT);
    draw_emote_props(scene_projection(data.viewmodel_fov_degrees, data.area_extent) * view);
}

// The emote props collected this frame: the opaque pieces, then the smoke
// and beer blended over them.
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
            const primitive_buffers& mesh = prop.mesh == prop_mesh::cylinder ? cylinder_
                                            : prop.mesh == prop_mesh::sphere ? ball_
                                                                             : panel_;
            set_terrain_draw_state(terrain_shader_, prop.model, view_proj, prop.color, prop.alpha, prop.glow, false);
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
        set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), proj * view, data.flight_path_color, alpha,
                               colors_.glow.flight_path, false);
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
    const course_map_layout layout = make_course_map_layout(data, overlay_grid{picture_.width, picture_.height});
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
    overlay_batch& batch = overlay_pass_.begin(profile, overlay_grid{picture_.width, picture_.height});
    if (data.show_course_results) {
        draw_course_results(batch, text, data.scorecard);
    } else {
        if (data.show_course_map) {
            render_course_map(data, text);
        }
        draw_hud(overlay_pass_.batch(), text, data, view_proj);
        if (data.god_menu) {
            draw_god_menu(overlay_pass_.batch(), text, *data.god_menu);
        }
        draw_datestamp(overlay_pass_.batch(), text, data.vcr);
        draw_startup_menu(overlay_pass_.batch(), text, data.startup_menu, colors_.ground);
    }
    draw_vcr_tag(overlay_pass_.batch(), text, data.vcr);
    if (data.show_fps) {
        render_debug_overlay(data, text, profile);
    }

    overlay_pass_.flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    gpu_timers_.end();
}

void renderer::render_tape(const unsigned int picture, const float seconds, const float tracking) {
    gpu_timers_.begin(gpu_profile_stage::tape);
    tape_pass_.draw(picture, seconds, tracking);
    gpu_timers_.end();
}
