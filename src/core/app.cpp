#include "core/app.h"

#include "core/event_loop.h"
#include "core/startup_flow.h"
#include "game/asset_resolver.h"
#include "game/course_world_loader.h"
#include "game/game_content.h"
#include "game/progression.h"
#include "game/save_manager.h"
#include "game/scorecard.h"
#include "game/text_ids.h"
#include "physics/terrain.h"
#include "profiling/profiling.h"
#include "renderer/render_mesh_chunks.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;

glm::vec3 aim_direction(const float aim_angle) {
    return glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
}

float radians(const float degrees) {
    return degrees * pi / 180.0f;
}

float terrain_height_at(const game_tuning& tuning, const glm::vec3& position, frame_profile* profile = nullptr) {
    const terrain_sample sample = sample_terrain_mesh(tuning.terrain_mesh_data, position, tuning.ground_y);
    record_terrain_sample(profile, sample.triangles_tested);
    return sample.point.y;
}

glm::vec3 render_color_for_terrain_material(const terrain_material material, const float distance_from_center, const float width) {
    const float half_width = std::max(0.001f, width * 0.5f);
    const float edge_amount = std::max(0.0f, std::min(1.0f, std::abs(distance_from_center) / half_width));

    switch (material) {
    case terrain_material::fairway:
        return glm::vec3(0.17f + edge_amount * 0.04f,
                         0.44f - edge_amount * 0.08f,
                         0.17f + edge_amount * 0.02f);
    case terrain_material::rough:
        return glm::vec3(0.12f, 0.27f, 0.12f);
    case terrain_material::green:
        return glm::vec3(0.20f, 0.68f, 0.28f);
    case terrain_material::bunker:
        return glm::vec3(0.66f, 0.55f, 0.22f);
    case terrain_material::water:
        return glm::vec3(0.10f, 0.22f, 0.60f);
    default:
        return glm::vec3(0.16f, 0.38f, 0.16f);
    }
}

std::vector<render_terrain_vertex> make_render_terrain_vertices(const terrain_mesh& mesh) {
    std::vector<render_terrain_vertex> vertices;
    vertices.reserve(mesh.vertices.size());

    for (const terrain_vertex& vertex : mesh.vertices) {
        render_terrain_vertex render_vertex;
        render_vertex.position = vertex.position;
        render_vertex.normal = vertex.normal;
        render_vertex.color = render_color_for_terrain_material(vertex.material, vertex.distance_from_center, mesh.width);
        vertices.push_back(render_vertex);
    }

    return vertices;
}

void set_material_overlay_render_mesh(render_static_mesh& mesh, const game_tuning& tuning) {
    constexpr float overlay_lift = 0.045f;
    const terrain_mesh overlay_mesh = build_material_overlay_mesh(tuning.terrain_mesh_data,
                                                                  tuning.course.material_zones,
                                                                  overlay_lift);
    mesh.vertices = make_render_terrain_vertices(overlay_mesh);
    mesh.indices = overlay_mesh.indices;
}

void append_render_terrain_mesh(std::vector<render_terrain_vertex>& vertices,
                                std::vector<std::uint32_t>& indices,
                                const terrain_mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    const std::uint32_t index_offset = static_cast<std::uint32_t>(vertices.size());
    const std::vector<render_terrain_vertex> appended_vertices = make_render_terrain_vertices(mesh);
    vertices.insert(vertices.end(), appended_vertices.begin(), appended_vertices.end());
    indices.reserve(indices.size() + mesh.indices.size());
    for (const std::uint32_t index : mesh.indices) {
        indices.push_back(index_offset + index);
    }
}

// Pure field copy from the cached, already terrain-anchored collision bodies.
// Refreshed only when the static anchor cache is rebuilt (same key the tree
// instance buffers use), so a normal frame neither allocates nor converts.
const std::vector<render_tree>& refresh_render_tree_cache(render_tree_cache& cache,
                                                          const static_anchor_cache& anchors) {
    if (cache.valid &&
        cache.revision == anchors.revision &&
        cache.source_count == anchors.tree_bodies.size()) {
        return cache.trees;
    }

    cache.trees.clear();
    cache.trees.reserve(anchors.tree_bodies.size());
    for (const tree_collision_body& body : anchors.tree_bodies) {
        render_tree render;
        render.base = body.base;
        render.trunk_radius = body.trunk_radius;
        render.trunk_height = body.trunk_height;
        render.leaf_radius = body.leaf_radius;
        render.leaf_height = body.leaf_height;
        cache.trees.push_back(render);
    }
    cache.revision = anchors.revision;
    cache.source_count = anchors.tree_bodies.size();
    cache.valid = true;
    return cache.trees;
}


void set_walking_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.player.yaw);
    data.camera_position = game.player.position + game.tuning.camera.walking_eye_offset;
    data.camera_target = data.camera_position + forward * game.tuning.camera.walking_target_distance;
}

void set_cart_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.cart.yaw);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    const glm::vec3 offset = right * game.tuning.camera.cart_eye_offset.x +
        up * game.tuning.camera.cart_eye_offset.y;

    data.camera_position = game.player.position + offset;
    data.camera_target = data.camera_position + forward * game.tuning.camera.cart_target_distance;
    data.camera_fov_degrees = game.tuning.camera.cart_fov_degrees;
}

void set_aiming_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.aim_angle);
    data.camera_position = game.ball.position - forward * 2.0f + glm::vec3(0.0f, 1.55f, 0.0f);
    data.camera_target = game.ball.position + forward * 12.0f + glm::vec3(0.0f, 1.05f, 0.0f);
}

void set_address_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.aim_angle);
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    data.camera_position = game.ball.position + left * 2.4f - forward * 0.7f + glm::vec3(0.0f, 2.0f, 0.0f);
    data.camera_target = game.ball.position + forward * 0.7f + glm::vec3(0.0f, 0.25f, 0.0f);
}

void set_follow_camera(render_data& data, const game_state& game) {
    data.camera_position = game.shot_camera_position;
    data.camera_target = follow_camera_target(game.ball.position);
}

std::vector<glm::vec3> estimate_aim_arc(const game_state& game, frame_profile* profile = nullptr) {
    std::vector<glm::vec3> points;
    if (game.selected_club >= game.tuning.clubs.size()) {
        return points;
    }

    const club_stats& club = game.tuning.clubs[game.selected_club].stats;
    const glm::vec3 forward = aim_direction(game.aim_angle);
    const float loft = radians(club.loft_degrees);
    const glm::vec3 launch_dir = glm::normalize(forward * std::cos(loft) + glm::vec3(0.0f, std::sin(loft), 0.0f));
    const glm::vec3 velocity = launch_dir * club.power;

    constexpr float gravity = -9.81f;
    constexpr float step_seconds = 0.18f;
    constexpr int max_points = 28;
    for (int i = 1; i <= max_points; ++i) {
        const float t = static_cast<float>(i) * step_seconds;
        glm::vec3 point = game.ball.position + velocity * t + glm::vec3(0.0f, 0.5f * gravity * t * t, 0.0f);
        const float terrain_height = terrain_height_at(game.tuning, point, profile);
        if (point.y < terrain_height) {
            point.y = terrain_height;
            points.push_back(point);
            break;
        }
        points.push_back(point);
    }

    return points;
}

controls_overlay_state make_controls_overlay_state(const input_state& input) {
    controls_overlay_state controls;
    controls.key_1_down = input.key_1.is_down;
    controls.key_2_down = input.key_2.is_down;
    controls.left_down = input.left.is_down;
    controls.right_down = input.right.is_down;
    controls.up_down = input.up.is_down;
    controls.down_down = input.down.is_down;
    controls.space_down = input.space.is_down;
    controls.shift_down = input.left_shift.is_down;
    controls.enter_down = input.enter.is_down;
    controls.backspace_down = input.backspace.is_down;
    controls.retee_down = input.retee.is_down;
    return controls;
}

std::vector<render_skill_progress> make_render_skills(const skill_progression& progression, const text_assets& text) {
    const std::array<const char*, 5> skills{{
        golf_swing_skill_id(),
        smoking_skill_id(),
        fitness_skill_id(),
        cart_driving_skill_id(),
        drifting_skill_id()
    }};

    std::vector<render_skill_progress> rows;
    rows.reserve(skills.size());
    for (const char* skill : skills) {
        render_skill_progress row;
        row.label = lookup_text(text, skill_text_key(skill).c_str());
        row.xp = skill_xp(progression, skill);
        row.level = skill_level(row.xp);
        row.xp_to_next = xp_to_next_level(progression, skill);
        rows.push_back(row);
    }
    return rows;
}

skill_icon_id skill_icon_for_id(const std::string& skill_id) {
    if (skill_id == golf_swing_skill_id()) {
        return skill_icon_id::golf_swing;
    }
    if (skill_id == smoking_skill_id()) {
        return skill_icon_id::smoking;
    }
    if (skill_id == fitness_skill_id()) {
        return skill_icon_id::fitness;
    }
    return skill_icon_id::generic;
}

std::vector<render_xp_drop> make_render_xp_drops(const std::vector<xp_drop>& drops) {
    std::vector<render_xp_drop> rows;
    rows.reserve(drops.size());
    for (const xp_drop& drop : drops) {
        if (drop.xp <= 0 || drop.age >= drop.lifetime) {
            continue;
        }

        render_xp_drop row;
        row.icon = skill_icon_for_id(drop.skill_id);
        row.xp = drop.xp;
        row.age = drop.age;
        row.lifetime = drop.lifetime;
        rows.push_back(row);
    }
    return rows;
}

// `anchors` must be current for `game` (see refresh_static_anchor_cache); static
// objects are read from it so this function does no per-object terrain sampling.
// Everything `data` borrows (trees, marker lists, flight path, meshes) is owned
// by `game`/`anchors`/the caches passed in, which all outlive the render call
// the returned data is handed to.
render_data make_render_data(const game_state& game,
                             const input_state& input,
                             const text_assets& text,
                             const static_anchor_cache& anchors,
                             const std::vector<render_tree>& trees,
                             const render_static_mesh& terrain_mesh,
                             const render_static_mesh& material_overlay_mesh,
                             frame_profile* profile = nullptr) {
    render_data data;
    data.ball_position = game.ball.position;
    data.player_position = game.player.position;
    data.player_yaw = game.player.yaw;
    data.tee_position = anchors.tee_anchor;
    data.pin_position = anchors.pin_anchor;
    data.cup_radius = game.tuning.course.cup_radius;
    data.ball_visual_radius_meters = game.tuning.scale.ball_visual_radius_meters;
    data.cup_visual_radius_meters = game.tuning.scale.cup_visual_radius_meters;
    data.pin_visual_height_meters = game.tuning.scale.pin_visual_height_meters;
    data.course_extent = game.tuning.course.extent;
    data.terrain_mesh = &terrain_mesh;
    data.material_overlay_mesh = &material_overlay_mesh;
    data.trees = &trees;
    data.trees_revision = anchors.revision;
    data.aim_angle = game.aim_angle;
    data.camera_fov_degrees = 60.0f;
    if (game.mode == game_mode::aiming) {
        data.aim_arc_points = estimate_aim_arc(game, profile);
    }
    data.ball_moving = ball_is_moving(game.ball, game.tuning);
    data.flight_path_points = &game.flight_path_points;
    data.flight_path_color = game.tuning.flight_path.color;
    data.flight_path_alpha = game.tuning.flight_path.alpha;
    data.flight_path_width = game.tuning.flight_path.line_width;
    data.show_flight_path = data.ball_moving && !game.flight_path_points.empty();
    data.show_interact_prompt = game.mode == game_mode::walking &&
        (can_interact_with_ball(game) || can_interact_with_hole_start(game));
    data.show_aim_indicator = game.mode == game_mode::aiming || game.mode == game_mode::addressing;
    data.shot_addressing = game.mode == game_mode::addressing;
    data.swing_timing = game.swing.phase == swing_phase::timing;
    data.show_power_meter = game.mode == game_mode::aiming || game.mode == game_mode::addressing || data.swing_timing;
    data.swing_power = game.swing.power;
    data.stroke_count = game.stroke_count;
    if (game.selected_club < game.tuning.clubs.size()) {
        data.selected_club_label = game.tuning.clubs[game.selected_club].label;
    }
    data.show_rangefinder = game.rangefinder_active;
    data.rangefinder_distance_meters = game.rangefinder_distance_meters;
    if (data.show_rangefinder) {
        data.rangefinder_distance_label = format_text(
            text, text_hud_rangefinder, {{"meters", std::to_string(rounded_rangefinder_meters(game.rangefinder_distance_meters))}});
    }
    data.show_course_map = game.course_map_active;
    data.show_scorecard = game.scorecard_active;
    data.show_skills_panel = game.skills_panel_active;
    data.show_course_results = game.round.finished;
    // Both are string-building; only pay for them on the frames that draw them.
    if (data.show_scorecard || data.show_course_results) {
        data.scorecard = build_scorecard_data(game, text.strings);
    }
    if (data.show_skills_panel) {
        data.skills = make_render_skills(game.save.skills, text);
    }
    data.xp_drops = game.round.finished ? std::vector<render_xp_drop>{} : make_render_xp_drops(game.xp_drops);
    data.cart_active = game.cart.active;
    data.cart_drifting = game.cart.drift_timer > 0.0f;
    data.cart_yaw = game.cart.active ? game.cart.yaw : game.player.yaw;
    data.cart_speed = game.cart.velocity;
    data.smoke_emote_active = game.smoke_emote.active;
    data.beer_emote_active = game.beer_emote.active;
    data.smoke_emote_elapsed = game.smoke_emote.elapsed;
    data.beer_emote_elapsed = game.beer_emote.elapsed;
    data.controls = make_controls_overlay_state(input);

    if (game.hub.available && game.hub.in_hub) {
        data.show_primary_hole_markers = false;
        data.tee_markers = &anchors.hub_tee_markers;
        data.pin_markers = &anchors.hub_pin_markers;
        data.start_markers = &anchors.hub_start_markers;
    }

    if (game.mode == game_mode::walking && game.cart.active) {
        set_cart_camera(data, game);
    } else if (game.mode == game_mode::walking) {
        set_walking_camera(data, game);
    } else if (game.mode == game_mode::aiming) {
        set_aiming_camera(data, game);
    } else if (game.mode == game_mode::addressing) {
        set_address_camera(data, game);
    } else {
        set_follow_camera(data, game);
    }

    return data;
}

std::string format_fps_label(const text_assets& text, const int fps, const int frame_ms) {
    return format_text(text,
                       text_hud_fps,
                       {{"fps", std::to_string(std::max(0, fps))}, {"ms", std::to_string(std::max(0, frame_ms))}});
}

// This frame's left click in overlay clip space (-1..1, y up), if any.
std::optional<glm::vec2> mouse_click_position(const input_state& input, SDL_Window* window) {
    if (!input.mouse_left.pressed || window == nullptr) {
        return std::nullopt;
    }

    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);
    const float x = static_cast<float>(input.mouse_x) / static_cast<float>(std::max(1, width)) * 2.0f - 1.0f;
    const float y = 1.0f - static_cast<float>(input.mouse_y) / static_cast<float>(std::max(1, height)) * 2.0f;
    return glm::vec2(x, y);
}

void play_ui_sounds(audio_engine& audio, const std::vector<ui_sound>& sounds) {
    for (const ui_sound sound : sounds) {
        switch (sound) {
        case ui_sound::move:
            audio.play("ui_move");
            break;
        case ui_sound::select:
            audio.play("ui_select");
            break;
        case ui_sound::back:
            audio.play("ui_back");
            break;
        }
    }
}

std::string club_hit_sound_id(const std::string& club_id) {
    if (club_id == "putter") {
        return "club_hit_putter";
    }
    if (club_id.find("wedge") != std::string::npos) {
        return "club_hit_wedge";
    }
    if (club_id.find("driver") != std::string::npos || club_id.find("wood") != std::string::npos) {
        return "club_hit_driver";
    }
    return "club_hit_iron";
}

std::string ball_land_sound_id(const terrain_material material) {
    if (material == terrain_material::water) {
        return "ball_splash";
    }
    if (material == terrain_material::bunker) {
        return "ball_land_bunker";
    }
    return "ball_land_grass";
}

void play_audio_event(audio_engine& audio, const audio_event& event) {
    switch (event.type) {
    case audio_event_type::swing_start:
        audio.play("swing_start");
        break;
    case audio_event_type::club_hit:
        audio.play(club_hit_sound_id(event.club_id));
        break;
    case audio_event_type::ball_land:
        audio.play(ball_land_sound_id(event.material));
        break;
    case audio_event_type::ball_tree_hit:
        audio.play("ball_tree_hit");
        break;
    case audio_event_type::ball_cup:
        audio.play("ball_cup");
        break;
    case audio_event_type::hole_complete:
        audio.play("hole_complete");
        break;
    case audio_event_type::club_change:
        audio.play("club_change");
        break;
    case audio_event_type::cart_start:
        audio.play("cart_start");
        break;
    case audio_event_type::cart_drift:
        audio.play("cart_drift");
        break;
    case audio_event_type::emote_smoke:
        audio.play("emote_smoke");
        break;
    case audio_event_type::emote_beer:
        audio.play("emote_beer");
        break;
    }
}

void drain_audio_events(audio_engine& audio, game_state& game) {
    for (const audio_event& event : game.audio_events) {
        play_audio_event(audio, event);
    }
    game.audio_events.clear();
}

bool save_completion_progress_changed(const save_data& before, const save_data& after) {
    return before.completed_course_ids != after.completed_course_ids ||
        before.current_course_id != after.current_course_id ||
        before.current_hole_index != after.current_hole_index ||
        before.hole_scores != after.hole_scores;
}
}

void app::return_to_menu() {
    return_to_main_menu(menu_);
    const std::uint64_t previous_render_revision = game_.terrain_render_revision;
    game_ = make_initial_game_state(game_.asset_root);
    continue_terrain_render_revision(game_, previous_render_revision);
    game_.save = save_slot_.save;
    audio_.stop_loop("cart_drive_loop");
    audio_.start_ambience("ambience_menu_vcr");
}

void app::mark_current_save_dirty() {
    if (!save_initialized_) {
        return;
    }
    mark_save_slot_dirty(save_slot_, game_.save);
}

bool app::persist_current_save() {
    if (!save_initialized_) {
        return false;
    }

    save_slot_.save = game_.save;
    return persist_save_slot(save_paths_, save_slot_);
}

void app::refresh_render_mesh_cache(frame_profile* profile) {
    const profile_scope timer(profile, profile_stage::refresh_render_mesh_cache);
    if (cached_terrain_revision_ == game_.terrain_render_revision &&
        cached_terrain_mesh_.revision == game_.terrain_render_revision &&
        cached_material_overlay_mesh_.revision == game_.terrain_render_revision) {
        return;
    }

    cached_terrain_mesh_.vertices = make_render_terrain_vertices(game_.tuning.terrain_mesh_data);
    cached_terrain_mesh_.indices = game_.tuning.terrain_mesh_data.indices;
    append_render_terrain_mesh(cached_terrain_mesh_.vertices,
                               cached_terrain_mesh_.indices,
                               game_.tuning.terrain_apron_mesh_data);
    cached_terrain_mesh_.bounds = compute_render_mesh_bounds(cached_terrain_mesh_.vertices);
    // Render chunks are part of the cached mesh: built once per terrain
    // revision, frustum culled per frame by the renderer.
    cached_terrain_mesh_.chunks = build_render_mesh_chunks(cached_terrain_mesh_.vertices,
                                                           cached_terrain_mesh_.indices);
    cached_terrain_mesh_.revision = game_.terrain_render_revision;

    set_material_overlay_render_mesh(cached_material_overlay_mesh_, game_.tuning);
    cached_material_overlay_mesh_.bounds = compute_render_mesh_bounds(cached_material_overlay_mesh_.vertices);
    cached_material_overlay_mesh_.chunks = build_render_mesh_chunks(cached_material_overlay_mesh_.vertices,
                                                                    cached_material_overlay_mesh_.indices);
    cached_material_overlay_mesh_.revision = game_.terrain_render_revision;

    cached_terrain_revision_ = game_.terrain_render_revision;
}

void app::present_frame(render_data& data, frame_profile* profile) {
    data.show_fps = show_fps_;
    data.fps_label = format_fps_label(text_, displayed_fps_, displayed_frame_ms_);
    data.profile_summary = profiler_.published;
    renderer_.render(data, text_, profile);
    if (profile != nullptr) {
        // Culling happens inside the renderer; the profile only records it.
        const renderer_cull_stats& cull = renderer_.cull_stats();
        profile->visible_chunks = cull.terrain.chunks_visible + cull.material_overlay.chunks_visible;
        profile->culled_chunks = cull.terrain.chunks_culled + cull.material_overlay.chunks_culled;
        profile->chunk_draw_ranges = cull.terrain.draw_ranges + cull.material_overlay.draw_ranges;
        profile->chunk_indices_drawn = cull.terrain.indices_drawn + cull.material_overlay.indices_drawn;
        profile->chunk_indices_total = cull.terrain.indices_total + cull.material_overlay.indices_total;
        profile->trees_visible = cull.trees_visible;
    }
    {
        const profile_scope timer(profile, profile_stage::window_swap);
        window_.swap();
    }
}

bool app::boot_into_course(const std::string& course_id) {
    const auto match = std::find_if(content_.courses.begin(),
                                    content_.courses.end(),
                                    [&course_id](const course_definition& course) {
                                        return course.id == course_id;
                                    });
    if (match == content_.courses.end()) {
        SDL_Log("GOLFPP_COURSE=%s: no such course, starting at the menu", course_id.c_str());
        return false;
    }

    if (!start_game_course(game_, *match)) {
        SDL_Log("GOLFPP_COURSE=%s: course failed to load, starting at the menu", course_id.c_str());
        return false;
    }

    enter_playing(menu_);
    SDL_Log("GOLFPP_COURSE=%s: booted straight into '%s' (%d holes)",
            course_id.c_str(),
            match->name.c_str(),
            match->hole_count);
    return true;
}

bool app::init(const startup_options& options) {
    if (!window_.init("golf++", 1280, 720, options.vsync)) {
        return false;
    }

    if (!renderer_.init(window_.sdl_window())) {
        window_.shutdown();
        return false;
    }

    char* base_path = SDL_GetBasePath();
    const std::string asset_root = resolve_asset_root(base_path != nullptr ? base_path : "");
    if (base_path != nullptr) {
        SDL_free(base_path);
    }

    std::optional<text_assets> text = load_text_assets(asset_root);
    if (!text) {
        SDL_Log("Failed to load text assets (%s, %s, %s) from %s",
                text_font_path,
                text_strings_path,
                text_styles_path,
                asset_root.c_str());
        renderer_.shutdown();
        window_.shutdown();
        return false;
    }
    text_ = std::move(*text);
    // Typing only reaches input_state::text_typed while a text field is focused.
    set_text_input_enabled(false);

    game_ = make_initial_game_state(asset_root);
    content_ = load_game_content(asset_root);
    hole_options_ = load_startup_holes(asset_root);

    char* pref_path = SDL_GetPrefPath("golfplusplus", "golf++");
    const std::string save_root = pref_path != nullptr
        ? std::string(pref_path)
        : (std::filesystem::path(asset_root) / "saves").string();
    save_paths_ = default_save_paths(save_root);
    save_slot_ = load_or_create_save_slot(save_paths_, game_.save);
    game_.save = save_slot_.save;
    save_initialized_ = true;
    if (!save_slot_.loaded_existing_profile || !save_slot_.loaded_existing_save) {
        persist_current_save();
    }
    if (pref_path != nullptr) {
        SDL_free(pref_path);
    }

    // Startup-only shortcut for the release performance pass: skip the menu
    // and open the requested course directly. Falls through to the menu when
    // the id is empty or unknown, so gameplay behaviour is otherwise identical.
    const bool booted_course = !options.boot_course_id.empty() && boot_into_course(options.boot_course_id);

    audio_.init();
    audio_.load_manifest(std::filesystem::path(asset_root) / "audio" / "sounds.json");
    audio_.start_ambience(booted_course ? "ambience_course_day" : "ambience_menu_vcr");
    running_ = true;
    return true;
}

void app::run() {
    Uint64 previous_counter = SDL_GetPerformanceCounter();
    const double performance_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    bool cart_loop_active = false;

    const auto make_frame_render_data = [this](frame_profile* profile) {
        const profile_scope timer(profile, profile_stage::make_render_data);
        // No-op unless terrain_render_revision changed without a rebuild.
        refresh_static_anchor_cache(game_, profile);
        return make_render_data(game_,
                                input_,
                                text_,
                                game_.static_anchors,
                                refresh_render_tree_cache(render_trees_, game_.static_anchors),
                                cached_terrain_mesh_,
                                cached_material_overlay_mesh_,
                                profile);
    };

    while (running_) {
        const Uint64 current_counter = SDL_GetPerformanceCounter();
        const double raw_elapsed_seconds = static_cast<double>(current_counter - previous_counter) / performance_frequency;
        previous_counter = current_counter;
        const float raw_dt = std::max(0.0f, static_cast<float>(raw_elapsed_seconds));
        const float simulation_dt = std::min(raw_dt, 0.05f);

        input_.reset_frame();
        poll_events(input_);

        if (input_.ctrl.pressed) {
            show_fps_ = !show_fps_;
        }

        profiler_end_frame(profiler_, raw_dt);
        profiler_.enabled = show_fps_;
        profiler_begin_frame(profiler_);
        frame_profile* profile = profiler_frame(profiler_);

        fps_elapsed_seconds_ += raw_dt;
        ++fps_frame_count_;
        if (fps_elapsed_seconds_ >= 0.25f) {
            displayed_fps_ = static_cast<int>(std::floor(static_cast<float>(fps_frame_count_) / fps_elapsed_seconds_ + 0.5f));
            displayed_frame_ms_ = static_cast<int>(std::floor(fps_elapsed_seconds_ * 1000.0f / static_cast<float>(fps_frame_count_) + 0.5f));
            fps_elapsed_seconds_ = 0.0f;
            fps_frame_count_ = 0;
        }

        if (menu_.flow != startup_flow::playing) {
            if (input_.quit_requested) {
                running_ = false;
            }

            const startup_menu_result result = update_startup_menu(menu_,
                                                                   input_,
                                                                   mouse_click_position(input_, window_.sdl_window()),
                                                                   hole_options_,
                                                                   content_);
            play_ui_sounds(audio_, result.sounds);
            if (result.action == startup_action::quit) {
                running_ = false;
            } else if (result.action == startup_action::start_course && start_game_course(game_, result.course)) {
                enter_playing(menu_);
                audio_.start_ambience("ambience_course_day");
            }

            refresh_render_mesh_cache(profile);
            render_data data = make_frame_render_data(profile);
            data.startup_menu = make_startup_menu_render_data(menu_, hole_options_, content_, text_);
            present_frame(data, profile);
            continue;
        }

        if (game_.round.finished) {
            if (input_.quit_requested) {
                running_ = false;
            }

            const bool leave_results = input_.enter.pressed ||
                input_.space.pressed ||
                input_.escape.pressed ||
                input_.backspace.pressed;
            if (leave_results) {
                audio_.play("ui_select");
                return_to_menu();
                cart_loop_active = false;
            }

            refresh_render_mesh_cache(profile);
            render_data data = make_frame_render_data(profile);
            if (menu_.flow != startup_flow::playing) {
                data.startup_menu = make_startup_menu_render_data(menu_, hole_options_, content_, text_);
            }
            present_frame(data, profile);
            continue;
        }

        bool confirm_opened_this_frame = false;
        if (!menu_.confirm_active && input_.escape.pressed && game_.mode == game_mode::walking) {
            open_confirm_menu(menu_);
            confirm_opened_this_frame = true;
        }

        if (menu_.confirm_active) {
            if (input_.quit_requested) {
                running_ = false;
            } else if (!confirm_opened_this_frame) {
                const confirm_menu_result result = update_confirm_menu(menu_,
                                                                       input_,
                                                                       mouse_click_position(input_, window_.sdl_window()));
                play_ui_sounds(audio_, result.sounds);
                if (result.leave_round) {
                    return_to_menu();
                    cart_loop_active = false;
                }
            }

            refresh_render_mesh_cache(profile);
            render_data data = make_frame_render_data(profile);
            data.startup_menu = make_confirm_menu_render_data(menu_, text_);
            present_frame(data, profile);
            continue;
        }

        const save_data save_before_update = game_.save;
        {
            const profile_scope timer(profile, profile_stage::update_game);
            update_game(game_, input_, simulation_dt, profile);
        }
        if (save_completion_progress_changed(save_before_update, game_.save)) {
            mark_current_save_dirty();
            persist_current_save();
        }
        drain_audio_events(audio_, game_);

        const bool cart_moving = game_.cart.active && std::abs(game_.cart.velocity) > 0.05f;
        if (cart_moving && !cart_loop_active) {
            audio_.play_loop("cart_drive_loop");
            cart_loop_active = true;
        } else if (!cart_moving && cart_loop_active) {
            audio_.stop_loop("cart_drive_loop");
            cart_loop_active = false;
        }

        if (input_.quit_requested) {
            running_ = false;
        }

        refresh_render_mesh_cache(profile);
        render_data data = make_frame_render_data(profile);
        present_frame(data, profile);
    }

    if (save_initialized_ && menu_.flow == startup_flow::playing) {
        mark_current_save_dirty();
        persist_current_save();
    } else if (save_initialized_ && save_slot_.profile.dirty) {
        persist_save_slot(save_paths_, save_slot_);
    }
}

void app::shutdown() {
    audio_.shutdown();
    renderer_.shutdown();
    window_.shutdown();
}
