#include "renderer/renderer.h"

#include "game/model_kinds.h"
#include "renderer/cart_batch.h"
#include "renderer/course_map_overlay.h"
#include "renderer/held_props.h"
#include "renderer/prop_glow.h"
#include "renderer/gl_loader.h"
#include "renderer/hud_overlay.h"
#include "renderer/menu_overlay.h"
#include "renderer/online_overlay.h"
#include "renderer/primitive_mesh.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/scorecard_overlay.h"
#include "renderer/bag_batch.h"
#include "renderer/shadow_casters.h"
#include "renderer/swing_club.h"
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

constexpr float flight_path_lift = 0.02f;
constexpr int grass_texture_unit = 1;
constexpr const char* grass_texture_path = "world/ground/rough_grass.bmp";
constexpr int path_texture_unit = 6;
constexpr const char* path_texture_path = "world/ground/cart_path.bmp";
constexpr const char* fence_net_texture_path = "world/props/fence_net.bmp";
constexpr float min_far_plane = 160.0f;
constexpr float far_plane_extent_scale = 2.5f;

void delete_vertex_array(unsigned int& vao) {
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}
}

bool renderer::init(SDL_Window* window, const std::string& asset_root, const picture_style& picture,
                    const lighting_palette& palette, const prop_catalog& props) {
    window_ = window;
    picture_style_ = picture;
    colors_ = palette.colors;
    water_style_ = palette.water;
    grass_style_ = palette.grass;
    terrain_lod_style_ = palette.terrain_lod;
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
        tree_renderer_.init(path("tree.vert"), path("tree.frag"), palette.trees, palette.tree_impostor) &&
        world_marker_renderer_.init(path("world_marker.vert"), path("lit_flat.frag"), true) &&
        tee_box_renderer_.init(path("tee_box.vert"), path("tee_box.frag")) &&
        cup_renderer_.init(path("world_marker.vert"), path("lit_flat.frag")) &&
        prop_renderer_.init(path("prop.vert"), path("prop.frag")) &&
        blob_shadow_renderer_.init(path("blob_shadow.vert"), path("blob_shadow.frag")) &&
        shadow_map_pass_.init(path("shadow_map.vert"), path("shadow_map.frag"), palette.shadows) &&
        lamp_shadow_pass_.init(path("shadow_map.vert"), path("shadow_map.frag"), palette.shafts) &&
        sun_shaft_pass_.init(path(""), screen_vao_, palette.shafts, shadow_map_unit, lamp_map_unit) &&
        hole_sign_renderer_.init(path("hole_sign.vert"), path("hole_sign.frag")) &&
        fence_renderer_.init(path("fence.vert"), path("fence.frag"),
                             (std::filesystem::path(asset_root) / fence_net_texture_path).string()) &&
        floodlight_renderer_.init(path("floodlight.vert"), path("floodlight.frag")) &&
        water_renderer_.init(path("water.vert"), path("water.frag"), path("underwater.vert"), path("underwater.frag"), screen_vao_) &&
        grass_renderer_.init(path("grass_blade.vert"), path("grass_blade.frag")) &&
        weather_renderer_.init(path("weather_streak.vert"), path("weather_streak.frag"),
                               std::max(palette.rain.max_drops, palette.wind.max_specks)) &&
        overlay_pass_.init(path("overlay.vert"), path("overlay.frag")) &&
        course_map_renderer_.init(path("course_map_sheet.vert"), path("course_map_sheet.frag")) &&
        backdrop_pass_.init(asset_root, path("backdrop.vert"), path("backdrop.frag"), screen_vao_, palette.sky);
    if (!ok) {
        return false;
    }
    tree_renderer_.set_falling_leaves(palette.falling_leaves);

    const std::optional<rgba_image> grass = load_bmp_file(std::filesystem::path(asset_root) / grass_texture_path);
    if (!grass || !grass_texture_.upload(*grass, texture_sampling::tiled_detail)) {
        SDL_Log("%s is missing or not an uncompressed 24/32-bit BMP", grass_texture_path);
        return false;
    }
    const std::optional<rgba_image> asphalt = load_bmp_file(std::filesystem::path(asset_root) / path_texture_path);
    if (!asphalt || !path_texture_.upload(*asphalt, texture_sampling::tiled_detail)) {
        SDL_Log("%s is missing or not an uncompressed 24/32-bit BMP", path_texture_path);
        return false;
    }
    terrain_shader_.use();
    terrain_shader_.set_int("u_grass", grass_texture_unit);
    terrain_shader_.set_int("u_path", path_texture_unit);

    gpu_timers_.init();
    return true;
}

std::string renderer::shader_path(const std::string& asset_root, const char* name) {
    return (std::filesystem::path(asset_root) / "picture" / "shaders" / name).string();
}

void renderer::shutdown() {
    gpu_timers_.shutdown();
    lighting_uniforms_.shutdown();
    tree_renderer_.shutdown();
    world_marker_renderer_.shutdown();
    tee_box_renderer_.shutdown();
    cup_renderer_.shutdown();
    prop_renderer_.shutdown();
    blob_shadow_renderer_.shutdown();
    shadow_map_pass_.shutdown();
    sun_shaft_pass_.shutdown();
    lamp_shadow_pass_.shutdown();
    hole_sign_renderer_.shutdown();
    fence_renderer_.shutdown();
    floodlight_renderer_.shutdown();
    water_renderer_.shutdown();
    reflection_pass_.shutdown();
    grass_renderer_.shutdown();
    weather_renderer_.shutdown();
    overlay_pass_.shutdown();
    course_map_renderer_.shutdown();
    backdrop_pass_.shutdown();
    grass_texture_.shutdown();
    path_texture_.shutdown();
    terrain_shader_.shutdown();
    ball_shader_.shutdown();
    camcorder_pass_.shutdown();
    eyepiece_pass_.shutdown();
    tape_pass_.shutdown();
    tv_pass_.shutdown();
    flight_path_buffer_.shutdown();
    delete_vertex_array(flight_path_vao_);

    terrain_renderer_.shutdown();
    for (primitive_buffers* buffers : {&ground_, &ball_}) {
        storage_.delete_buffer(buffers->vbo);
        delete_vertex_array(buffers->vao);
        *buffers = primitive_buffers{};
    }
    storage_.delete_buffer(screen_vbo_);
    delete_vertex_array(screen_vao_);

    scene_fbo_.shutdown();
    window_ = nullptr;
}

void renderer::render(const render_data& data, const text_assets& text) {
    frame_profile* profile = allocations_.frame;
    if (window_ == nullptr) {
        return;
    }

    const profile_scope render_timer(profile, profile_stage::render);
    gpu_timers_.begin_frame(profile);

    int screen_width = 0;
    int screen_height = 0;
    SDL_GL_GetDrawableSize(window_, &screen_width, &screen_height);
    screen_width = std::max(1, screen_width);
    screen_height = std::max(1, screen_height);
    if (!ensure_framebuffer_size(screen_width, screen_height)) {
        gpu_timers_.end_frame();
        return;
    }

    // Rebuilt with the course; the bus's doors are drawn each frame.
    const bool new_course = refresh_static_geometry(data, profile);
    advance_bus_doors(data, new_course);
    const glm::mat4 proj = scene_projection(data.camera_fov_degrees, data.area_extent);
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
    if (!data.vcr.still_frame || !camcorder_pass_.has_frame()) {
        const profile_scope scene_timer(profile, profile_stage::render_scene);
        const std::vector<tree_body> no_trees;
        tree_renderer_.set_trees(data.trees != nullptr ? *data.trees : no_trees, data.trees_revision, profile);
        tree_renderer_.set_seasons(data.tree_species, profile);
        tree_renderer_.set_motion(data.clock_seconds, glm::vec2(data.weather.surface_wind.x, data.weather.surface_wind.z));
        stage_frame_geometry(data, profile);
        grass_renderer_.fetch_ahead(data.grass, data.camera_position, grass_style_);
        const prop_caster_source casting{prop_renderer_.static_buffer(), prop_renderer_.static_opaque_runs(),
                                         prop_renderer_.frame_buffer(), prop_frame_.ranges(prop_view::sun_shadow, false),
                                         prop_renderer_.static_revision(), &prop_frame_.batch()};
        shadow_map_pass_.draw(tree_renderer_, floodlight_renderer_, data.floodlights,
                              marker_caster_source{world_marker_renderer_.buffer(), world_marker_renderer_.instance_buffer(),
                                                   &world_marker_batch_},
                              casting,
                              data.lighting.sun_direction, data.lighting.shadows.sun_share, data.camera_position,
                              gpu_timers_, profile);
        glEnable(GL_DEPTH_TEST);
        const glm::vec3 sky = data.lighting.sky_zenith;
        // A pond's pixels read what lies beside them, so with one in either view nothing is masked.
        const glm::mat4 zoom_proj = zoomed_projection(proj, data.rangefinder_eyepiece.magnification);
        const bool mask_eyepiece = eyepiece_masking_ && data.show_rangefinder && !data.camera_underwater &&
            !water_may_show(data, proj * view) && !water_may_show(data, zoom_proj * view);
        if (data.show_rangefinder) {
            const profile_pass_scope pass(profile, profile_pass::zoom);
            gpu_timers_.begin(gpu_profile_stage::zoom);
            eyepiece_pass_.begin_zoom(sky, mask_eyepiece);
            render_scene(view, zoom_proj, data, eyepiece_pass_.zoom_target(), profile,
                         false, nullptr, 0.0f, true);
            eyepiece_pass_.end_zoom();
            gpu_timers_.end();
        }
        scene_fbo_.bind();
        glViewport(0, 0, picture_.width, picture_.height);
        glClearColor(sky.r, sky.g, sky.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const draw_target scene_view{scene_fbo_.id(), picture_.width, picture_.height, false};
        const hidden_disc hidden = mask_eyepiece ? eyepiece_pass_.mask_scene(proj * view, picture_.width, picture_.height) : hidden_disc{};
        render_scene(view, proj, data, scene_view, profile, false, nullptr, 0.0f, false, hidden);
        if (!data.camera_underwater) {
            const frame_lighting_block& lighting = lighting_uniforms_.block();
            lamp_shadow_pass_.update(lighting_uniforms_.lights(), glm::vec3(lighting.haze_eye), tree_renderer_,
                                     floodlight_renderer_, data.floodlights, shadow_map_pass_.pose_tolerance_meters(),
                                     gpu_timers_, profile);
            sun_shaft_pass_.draw(lighting, shadow_map_pass_, lamp_shadow_pass_, scene_fbo_.depth_texture(), proj * view,
                                 gpu_timers_);
            scene_fbo_.bind();
            glViewport(0, 0, picture_.width, picture_.height);
        }
        if (data.camera_underwater) {
            water_renderer_.draw_underwater(proj * view, scene_view, water_style_, data.camera_water_level, data.clock_seconds, gpu_timers_, profile);
        }
        render_viewmodel(view, data, profile);
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

    gpu_timers_.end_frame();
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
    draw_loading_screen(overlay_pass_.begin(nullptr, ui_grid()), text);
    overlay_pass_.flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    tv_pass_.draw(tape_pass_.color_texture(), picture_, screen_width, screen_height, border);
}

bool renderer::init_geometry() {
    const auto make_primitive = [this](primitive_buffers& buffers, const std::vector<float>& vertices, const bool with_vao) {
        buffers.vertex_count = static_cast<int>(vertices.size() / 6);
        if (with_vao) {
            glGenVertexArrays(1, &buffers.vao);
            glBindVertexArray(buffers.vao);
        }
        glGenBuffers(1, &buffers.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
        storage_.buffer(buffers.vbo, GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
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
    storage_.buffer(screen_vbo_, GL_ARRAY_BUFFER, sizeof(screen_vertices), screen_vertices, GL_STATIC_DRAW);
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
    const int preset = std::clamp(scene_preset_, 0, static_cast<int>(picture_style_.scene_lines.size()) - 1);
    picture_ = picture_size_for_window(screen_width, screen_height, picture_style_.scene_lines[static_cast<std::size_t>(preset)],
                                       picture_style_.ui_lines);
    camcorder_pass_.set_grain_cell(picture_.unit);
    const bool scene_ok = (scene_fbo_.width() == picture_.width && scene_fbo_.height() == picture_.height) ||
        scene_fbo_.init(picture_.width, picture_.height,
                        framebuffer_format{framebuffer_color::hdr, framebuffer_depth::sampled, false, false});
    return scene_ok && sun_shaft_pass_.ensure_size(picture_.width, picture_.height, scene_fbo_.color_texture()) &&
        camcorder_pass_.ensure_size(picture_.width, picture_.height) &&
        eyepiece_pass_.ensure_size(picture_.width, picture_.height) &&
        tape_pass_.ensure_size(picture_.width, picture_.height);
}

// What is pinned to the camera, what I hold, drawn over the scene at the
// field of view it was posed for (render_data.h).
void renderer::render_viewmodel(const glm::mat4& view, const render_data& data, frame_profile* profile) {
    const auto held = [this](const std::optional<render_item_use>& use) -> std::optional<held_use> {
        const auto model = use ? props_.find(use->model) : props_.end();
        return model != props_.end() ? std::optional<held_use>(held_use{&model->second, use->elapsed}) : std::nullopt;
    };
    const std::optional<held_use> smokable = held(data.smoking);
    const std::optional<held_use> drinkable = held(data.drinking);
    if (!smokable && !drinkable) {
        return;
    }
    viewmodel_batch_.clear();
    append_held_props(viewmodel_batch_, camera_prop_holder(data.camera_position, data.camera_target, smokable, drinkable),
                      colors_.glow);
    // Never cut into by the world it sits in.
    glClear(GL_DEPTH_BUFFER_BIT);
    gpu_timers_.begin(gpu_profile_stage::props);
    prop_renderer_.shader().use();
    shadow_map_pass_.bind(prop_renderer_.shader(), shadow_map_unit);
    prop_renderer_.draw(viewmodel_batch_, scene_projection(data.viewmodel_fov_degrees, data.area_extent) * view, profile);
    gpu_timers_.end();
}

// The doors swing towards what the game says (render_data.h), taking the
// model's swing time; they stand at it, not swing, on a new course or while
// the game does not yet know it (a join mid-boarding).
void renderer::advance_bus_doors(const render_data& data, const bool snap) {
    const prop_model* bus = data.world_props ? placed_bus(*data.world_props, props_) : nullptr;
    const float elapsed = std::clamp(data.clock_seconds - bus_door_clock_, 0.0f, 0.25f);
    bus_door_clock_ = data.clock_seconds;
    bus_door_seeded_ = true;
    const std::optional<float> before = snap || !data.bus_doors_settled ? std::nullopt : std::optional<float>(bus_door_progress_);
    bus_door_progress_ = door_swing_step(before, data.bus_doors_open, elapsed, bus != nullptr ? bus->number("door_swing_seconds") : 0.0f);
}

void renderer::draw_scene_props(const bool reflected, const glm::mat4& view_proj, frame_profile* profile) {
    const std::array<glm::vec4, 2> glow = glow_floor_table(colors_.glow);
    gpu_timers_.begin(gpu_profile_stage::props);
    prop_renderer_.shader().use();
    shadow_map_pass_.bind(prop_renderer_.shader(), shadow_map_unit);
    prop_renderer_.draw_frame(prop_frame_, reflected ? prop_view::mirror : prop_view::scene, view_proj, glow, profile);
    prop_renderer_.draw_static(view_proj, glow, profile);
    gpu_timers_.end();
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
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(flight_path_vertices_.size()));
        record_draw_call(terrain_shader_.profile());
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
    const course_map_layout layout = make_course_map_layout(data, ui_grid());
    draw_course_map_paper(overlay_pass_.batch(), layout, colors_.map);
    if (data.course_map != nullptr) {
        // The ground is its own draw, between the paper and the marks.
        overlay_pass_.flush();
        course_map_renderer_.draw(*data.course_map, colors_.map, looks_revision_, layout.center, layout.half_size,
                                  nullptr);
    }
    draw_course_map_folds(overlay_pass_.batch(), layout, colors_.map);
    draw_course_map_marks(overlay_pass_.batch(), text, layout, data, colors_.map);
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
    overlay_batch& batch = overlay_pass_.begin(profile, ui_grid());
    if (data.show_course_results) {
        draw_course_results(batch, text, data.scorecard);
    } else {
        if (data.show_course_map) {
            render_course_map(data, text);
        }
        if (data.show_hud) {
            draw_hud(overlay_pass_.batch(), text, data, view_proj);
        }
        if (data.god_menu) {
            draw_god_menu(overlay_pass_.batch(), text, *data.god_menu);
        }
        draw_datestamp(overlay_pass_.batch(), text, data.vcr);
        draw_vcr_title(overlay_pass_.batch(), text, data.vcr);
        draw_startup_menu(overlay_pass_.batch(), text, data.startup_menu, colors_.ground);
    }
    draw_vcr_tag(overlay_pass_.batch(), text, data.vcr);
    draw_notice(overlay_pass_.batch(), text, data.look_notice, message_severity::error);
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
