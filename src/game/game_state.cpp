#include "game/game_state.h"

#include "core/input.h"
#include "game/asset_resolver.h"
#include "game/course_loader.h"
#include "game/course_world_loader.h"
#include "game/hole_loader.h"
#include "game/progression.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/terrain.h"
#include "physics/tree_collision.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr int min_visible_xp_drop = 5;
constexpr float default_xp_drop_lifetime = 2.4f;

glm::vec3 aim_direction(const float aim_angle) {
    return glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
}

float radians(const float degrees) {
    return degrees * pi / 180.0f;
}

float aim_angle_towards(const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 delta = to - from;
    const glm::vec3 flat(delta.x, 0.0f, delta.z);
    if (glm::length(flat) <= 0.0001f) {
        return 0.0f;
    }
    return std::atan2(flat.x, flat.z);
}

glm::vec3 address_camera_position(const game_state& state) {
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    return state.ball.position + left * 2.4f - forward * 0.7f + glm::vec3(0.0f, 2.0f, 0.0f);
}

void clear_flight_path(game_state& state) {
    state.flight_path_points.clear();
}

void append_flight_path_point(game_state& state, const glm::vec3& position) {
    const float min_spacing = std::max(0.01f, state.tuning.flight_path.min_point_spacing);
    if (!state.flight_path_points.empty()) {
        const glm::vec3 delta = position - state.flight_path_points.back();
        if (glm::length(delta) < min_spacing) {
            return;
        }
    }

    state.flight_path_points.push_back(position);
    const int max_points = std::max(2, state.tuning.flight_path.max_points);
    if (static_cast<int>(state.flight_path_points.size()) > max_points) {
        const std::size_t excess = state.flight_path_points.size() - static_cast<std::size_t>(max_points);
        state.flight_path_points.erase(state.flight_path_points.begin(),
                                       state.flight_path_points.begin() + static_cast<std::vector<glm::vec3>::difference_type>(excess));
    }
}

void push_audio_event(game_state& state, const audio_event_type type) {
    audio_event event;
    event.type = type;
    state.audio_events.push_back(event);
}

void push_club_audio_event(game_state& state, const audio_event_type type) {
    audio_event event;
    event.type = type;
    if (state.selected_club < state.tuning.clubs.size()) {
        event.club_id = state.tuning.clubs[state.selected_club].id;
    }
    state.audio_events.push_back(event);
}

void push_ball_land_audio_event(game_state& state, const terrain_material material) {
    audio_event event;
    event.type = audio_event_type::ball_land;
    event.material = material;
    state.audio_events.push_back(event);
}

void emit_xp_drop(game_state& state, const std::string& skill_id, const int xp) {
    if (skill_id.empty() || xp <= 0) {
        return;
    }

    for (xp_drop& drop : state.xp_drops) {
        if (drop.skill_id == skill_id) {
            drop.xp += xp;
            drop.age = 0.0f;
            drop.lifetime = default_xp_drop_lifetime;
            return;
        }
    }

    xp_drop drop;
    drop.skill_id = skill_id;
    drop.xp = xp;
    drop.lifetime = default_xp_drop_lifetime;
    state.xp_drops.push_back(drop);
}

terrain_sample terrain_sample_at(const game_tuning& tuning,
                                 const glm::vec3& position,
                                 frame_profile* profile = nullptr) {
    const terrain_sample sample = sample_terrain_mesh(tuning.terrain_mesh_data, position, tuning.ground_y);
    record_terrain_sample(profile, sample.triangles_tested);
    return sample;
}

terrain_sample terrain_sample_at(const game_tuning& tuning,
                                 const glm::vec3& position,
                                 const terrain_sample* previous_sample,
                                 frame_profile* profile = nullptr) {
    const terrain_sample sample = sample_terrain_mesh(tuning.terrain_mesh_data,
                                                     position,
                                                     tuning.ground_y,
                                                     previous_sample);
    record_terrain_sample(profile, sample.triangles_tested);
    return sample;
}

float terrain_height_at(const game_tuning& tuning, const glm::vec3& position, frame_profile* profile = nullptr) {
    return terrain_sample_at(tuning, position, profile).point.y;
}

std::vector<tree_collision_body> anchored_tree_bodies(const game_tuning& tuning, frame_profile* profile) {
    std::vector<tree_collision_body> trees;
    trees.reserve(tuning.course.trees.size());
    for (const tree_instance& tree : tuning.course.trees) {
        tree_collision_body body;
        body.base = tree_base_position(tuning, tree, profile);
        body.trunk_radius = tree.trunk_radius;
        body.trunk_height = tree.trunk_height;
        body.leaf_radius = tree.leaf_radius;
        body.leaf_height = tree.leaf_height;
        trees.push_back(body);
    }
    return trees;
}

enum class hub_marker_kind {
    tee,
    pin,
    start
};

std::vector<glm::vec3> anchored_hub_marker_positions(const game_tuning& tuning,
                                                     const std::vector<course_hub_hole_marker>& markers,
                                                     const hub_marker_kind kind,
                                                     frame_profile* profile) {
    std::vector<glm::vec3> positions;
    positions.reserve(markers.size());
    for (const course_hub_hole_marker& marker : markers) {
        const glm::vec3& authored = kind == hub_marker_kind::tee
            ? marker.tee_position
            : (kind == hub_marker_kind::pin ? marker.pin_position : marker.start_position);
        positions.push_back(terrain_anchor_position(tuning, authored, profile));
    }
    return positions;
}

float effective_cup_radius(const game_tuning& tuning) {
    return std::max(tuning.course.cup_radius, tuning.scale.cup_visual_radius_meters);
}

float distance_xz_squared(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

bool path_intersects_cup(const glm::vec3& start,
                         const glm::vec3& end,
                         const glm::vec3& cup_center,
                         const float radius,
                         const float max_center_height_above_cup) {
    const glm::vec3 segment(end.x - start.x, 0.0f, end.z - start.z);
    const float segment_len_sq = glm::dot(segment, segment);
    if (segment_len_sq <= 0.000001f) {
        return distance_xz_squared(end, cup_center) <= radius * radius &&
            end.y <= cup_center.y + max_center_height_above_cup;
    }

    const glm::vec3 to_cup(cup_center.x - start.x, 0.0f, cup_center.z - start.z);
    const float t = std::max(0.0f, std::min(1.0f, glm::dot(to_cup, segment) / segment_len_sq));
    const glm::vec3 closest = start + segment * t;
    return distance_xz_squared(closest, cup_center) <= radius * radius &&
        closest.y <= cup_center.y + max_center_height_above_cup;
}

bool contains_string(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void expand_bounds(glm::vec3& min_point, glm::vec3& max_point, const glm::vec3& point) {
    min_point.x = std::min(min_point.x, point.x);
    min_point.y = std::min(min_point.y, point.y);
    min_point.z = std::min(min_point.z, point.z);
    max_point.x = std::max(max_point.x, point.x);
    max_point.y = std::max(max_point.y, point.y);
    max_point.z = std::max(max_point.z, point.z);
}

const course_world_hole_start* find_hole_start(const course_world_definition& world, const std::size_t hole_index) {
    for (const course_world_hole_start& start : world.hole_starts) {
        if (start.hole_index == static_cast<int>(hole_index)) {
            return &start;
        }
    }
    if (hole_index < world.hole_starts.size()) {
        return &world.hole_starts[hole_index];
    }
    return nullptr;
}

void append_terrain_mesh(terrain_mesh& target, const terrain_mesh& source) {
    if (source.vertices.empty() || source.indices.empty()) {
        return;
    }

    const std::uint32_t index_offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    target.indices.reserve(target.indices.size() + source.indices.size());
    for (const std::uint32_t index : source.indices) {
        target.indices.push_back(index_offset + index);
    }
    target.section_count += source.section_count;
    target.cross_section_count = source.cross_section_count;
    target.width = std::max(target.width, source.width);
}

bool apply_course_world_to_tuning(game_state& state) {
    game_tuning& tuning = state.tuning;
    const course_world_definition& world = state.hub.world;
    const course_definition& course = state.active_course;

    terrain_mesh combined_terrain;
    terrain_mesh combined_apron;
    std::vector<material_zone> combined_zones;
    std::vector<tree_instance> combined_trees;
    std::vector<course_hub_hole_marker> markers;

    glm::vec3 min_point = world.spawn.position;
    glm::vec3 max_point = world.spawn.position;

    for (std::size_t hole_index = 0; hole_index < course.holes.size(); ++hole_index) {
        const course_world_hole_start* start = find_hole_start(world, hole_index);
        if (start == nullptr) {
            return false;
        }

        const std::string path = course_hole_path(state.asset_root, course, hole_index);
        const std::optional<hole_data> hole = load_hole_from_file(path);
        if (!hole) {
            return false;
        }

        terrain_spline local_terrain;
        local_terrain.control_points = hole->spline.control_points;
        local_terrain.width = std::max(hole->spline.width, hole->spline.rough_width);
        local_terrain.fairway_width = hole->spline.width;
        local_terrain.sample_count = 128;

        const terrain_mesh local_mesh = build_terrain_mesh(local_terrain, hole->material_zones, tuning.zone_tuning);
        const terrain_mesh local_apron = build_outer_rough_apron(local_mesh, local_terrain.width, 14);
        const terrain_mesh world_mesh = transform_course_world_terrain_mesh(local_mesh, *hole, *start);
        const terrain_mesh world_apron = transform_course_world_terrain_mesh(local_apron, *hole, *start);
        append_terrain_mesh(combined_terrain, world_mesh);
        append_terrain_mesh(combined_apron, world_apron);

        for (const terrain_vertex& vertex : world_mesh.vertices) {
            expand_bounds(min_point, max_point, vertex.position);
        }

        for (const material_zone& zone : hole->material_zones) {
            combined_zones.push_back(transform_course_world_material_zone(zone, *hole, *start));
        }
        for (const tree_instance& tree : hole->trees) {
            combined_trees.push_back(transform_course_world_tree_instance(tree, *hole, *start));
        }

        course_hub_hole_marker marker;
        marker.tee_position = course_world_hole_point(*hole, *start, hole->tee_position);
        marker.pin_position = course_world_hole_point(*hole, *start, hole->pin_position);
        marker.start_position = start->position;
        markers.push_back(marker);
    }

    if (combined_terrain.vertices.empty() || combined_terrain.indices.empty()) {
        return false;
    }

    tuning.course.id = world.id;
    tuning.course.name = world.name;
    tuning.course.par = 0;
    tuning.course.tee_position = world.spawn.position;
    tuning.course.pin_position = markers.empty() ? world.spawn.position : markers.front().pin_position;
    tuning.course.cup_radius = tuning.scale.cup_physics_radius_meters;
    tuning.course.extent = std::max(max_point.x - min_point.x, max_point.z - min_point.z) * 0.65f;
    tuning.course.spline.control_points.clear();
    tuning.course.spline.width = combined_terrain.width;
    tuning.course.spline.rough_width = combined_terrain.width;
    tuning.course.material_zones = combined_zones;
    tuning.course.trees = combined_trees;
    tuning.terrain.control_points.clear();
    tuning.terrain.width = combined_terrain.width;
    tuning.terrain.fairway_width = combined_terrain.width;
    tuning.terrain.sample_count = 128;
    // append_terrain_mesh builds these by hand, so the sampling index has to be
    // built once the combined meshes are final.
    tuning.terrain_mesh_data = build_terrain_mesh_index(std::move(combined_terrain));
    tuning.terrain_apron_mesh_data = build_terrain_mesh_index(std::move(combined_apron));
    tuning.ground_y = 0.0f;
    state.hub.hole_markers = markers;
    return true;
}

bool starter_club_id(const std::string& club_id) {
    return club_id == "putter" || club_id == "pitching_wedge" || club_id == "seven_iron";
}

bool club_available(const save_data& save, const club_definition& club) {
    return starter_club_id(club.id) || has_unlock(save.unlocked_items, club.id);
}

bool rangefinder_unlocked(const save_data& save) {
    return has_unlock(save.unlocked_items, rangefinder_unlock_id());
}

bool cart_unlocked(const save_data& save) {
    return has_unlock(save.unlocked_items, cart_unlock_id());
}

std::string selected_cigarette_unlock_id(const save_data& save) {
    if (has_unlock(save.unlocked_items, cigarette_longcut_unlock_id())) {
        return cigarette_longcut_unlock_id();
    }
    if (has_unlock(save.unlocked_items, cigarette_menthol_unlock_id())) {
        return cigarette_menthol_unlock_id();
    }
    if (has_unlock(save.unlocked_items, cigarette_filterless_unlock_id())) {
        return cigarette_filterless_unlock_id();
    }
    return "";
}

float cigarette_effect_duration(const std::string& unlock_id) {
    if (unlock_id == cigarette_longcut_unlock_id()) {
        return 8.0f;
    }
    if (unlock_id == cigarette_menthol_unlock_id() || unlock_id == cigarette_filterless_unlock_id()) {
        return 5.0f;
    }
    return 0.0f;
}

club_stats apply_cigarette_effect(const club_stats& base, const cigarette_effect_state& effect) {
    if (effect.remaining_seconds <= 0.0f) {
        return base;
    }

    club_stats adjusted = base;
    if (effect.unlock_id == cigarette_filterless_unlock_id()) {
        adjusted.spin_bias *= 0.90f;
    } else if (effect.unlock_id == cigarette_menthol_unlock_id()) {
        adjusted.timing_speed *= 0.85f;
    } else if (effect.unlock_id == cigarette_longcut_unlock_id()) {
        adjusted.spin_bias *= 0.94f;
        adjusted.timing_speed *= 0.92f;
    }
    return adjusted;
}

void sink_ball_in_cup(game_state& state) {
    push_audio_event(state, audio_event_type::ball_cup);
    const glm::vec3 pin_anchor = pin_anchor_position(state);
    state.ball.position = pin_anchor - glm::vec3(0.0f, state.ball.radius * 2.0f, 0.0f);
    state.ball.velocity = glm::vec3(0.0f);
    state.ball.spin = glm::vec3(0.0f);
    state.swing = swing_state{};
    state.mode = game_mode::walking;
    clear_flight_path(state);
}

bool complete_if_ball_reached_cup(game_state& state, const glm::vec3& previous_ball_position) {
    if (state.round.finished) {
        return false;
    }

    const glm::vec3 pin_anchor = pin_anchor_position(state);
    if (!path_intersects_cup(previous_ball_position,
                             state.ball.position,
                             pin_anchor,
                             effective_cup_radius(state.tuning),
                             std::max(state.ball.radius * 4.0f, state.tuning.scale.ball_visual_radius_meters * 2.0f))) {
        return false;
    }

    sink_ball_in_cup(state);
    complete_current_hole(state);
    return true;
}

void update_rangefinder_state(game_state& state, const input_state& input) {
    state.rangefinder_active = rangefinder_unlocked(state.save) && rangefinder_should_show(state.mode, input);
    state.rangefinder_distance_meters = compute_rangefinder_distance_meters(state.player.position,
                                                                            pin_anchor_position(state),
                                                                            state.tuning.scale.meters_per_world_unit);
    state.rangefinder_distance_label = format_rangefinder_distance(state.rangefinder_distance_meters);
}

void update_course_map_state(game_state& state, const input_state& input) {
    state.course_map_active = course_map_should_show(state.mode, input);
}

void update_scorecard_state(game_state& state, const input_state& input) {
    state.scorecard_active = scorecard_should_show(state.mode, input);
}

void update_skills_panel_state(game_state& state, const input_state& input) {
    state.skills_panel_active = input.caps_lock.is_down;
}

void clear_emote(game_state& state) {
    state.smoke_emote = emote_state{};
    state.beer_emote = emote_state{};
}

void trigger_emote(emote_state& emote) {
    emote.elapsed = 0.0f;
    emote.active = true;
}

void tick_emote(emote_state& emote, const float dt) {
    if (!emote.active) {
        return;
    }

    emote.elapsed += dt;
    if (emote.elapsed >= 2.0f) {
        emote = emote_state{};
    }
}

void tick_cigarette_effect(cigarette_effect_state& effect, const float dt) {
    if (effect.remaining_seconds <= 0.0f) {
        effect = cigarette_effect_state{};
        return;
    }

    effect.remaining_seconds = std::max(0.0f, effect.remaining_seconds - dt);
    if (effect.remaining_seconds <= 0.0f) {
        effect = cigarette_effect_state{};
    }
}

void update_emote_state(game_state& state, const input_state& input, const float dt) {
    if (input.key_1.pressed) {
        const std::string cigarette_id = selected_cigarette_unlock_id(state.save);
        if (!cigarette_id.empty()) {
            trigger_emote(state.smoke_emote);
            state.cigarette_effect.unlock_id = cigarette_id;
            state.cigarette_effect.remaining_seconds = cigarette_effect_duration(cigarette_id);
            award_skill_xp(state, smoking_skill_id(), 10);
            push_audio_event(state, audio_event_type::emote_smoke);
        }
    }
    if (input.key_2.pressed) {
        trigger_emote(state.beer_emote);
        push_audio_event(state, audio_event_type::emote_beer);
    }

    tick_emote(state.smoke_emote, dt);
    tick_emote(state.beer_emote, dt);
    tick_cigarette_effect(state.cigarette_effect, dt);
}

void update_walk_overlays(game_state& state, const input_state& input) {
    update_rangefinder_state(state, input);
    update_course_map_state(state, input);
    update_scorecard_state(state, input);
    update_skills_panel_state(state, input);
}

glm::vec3 ball_rest_position(const game_tuning& tuning,
                             const glm::vec3& position,
                             const float radius,
                             frame_profile* profile = nullptr) {
    const terrain_sample terrain = terrain_sample_at(tuning, position, profile);
    const glm::vec3 normal = glm::length(terrain.normal) > 0.00001f
        ? glm::normalize(terrain.normal)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    return terrain.point + normal * radius;
}

float ball_support_distance(const ball_state& ball, const terrain_sample& terrain) {
    const glm::vec3 normal = glm::length(terrain.normal) > 0.00001f
        ? glm::normalize(terrain.normal)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::dot(ball.position - terrain.point, normal);
}

glm::vec3 address_player_position(const game_state& state, frame_profile* profile = nullptr) {
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    glm::vec3 position = state.ball.position + left * state.tuning.player_stand_off_distance - forward * 0.4f;
    position.y = terrain_height_at(state.tuning, position, profile);
    return position;
}

void select_previous_club(game_state& state) {
    if (state.tuning.clubs.empty()) {
        return;
    }

    if (state.selected_club == 0) {
        state.selected_club = state.tuning.clubs.size() - 1;
    } else {
        --state.selected_club;
    }
    push_audio_event(state, audio_event_type::club_change);
}

void select_next_club(game_state& state) {
    if (state.tuning.clubs.empty()) {
        return;
    }

    state.selected_club = (state.selected_club + 1) % state.tuning.clubs.size();
    push_audio_event(state, audio_event_type::club_change);
}

void launch_ball(game_state& state) {
    if (state.tuning.clubs.empty()) {
        return;
    }

    const club_stats club = apply_cigarette_effect(state.tuning.clubs[state.selected_club].stats, state.cigarette_effect);
    const glm::vec3 forward = aim_direction(state.aim_angle);
    const float loft = radians(club.loft_degrees);
    const glm::vec3 launch_dir = glm::normalize(forward * std::cos(loft) + glm::vec3(0.0f, std::sin(loft), 0.0f));
    const float power = std::max(state.tuning.min_swing_power, state.swing.power);
    const float speed = club.power * power;

    state.shot_camera_position = address_camera_position(state);
    state.ball.velocity = launch_dir * speed;
    state.ball.spin = glm::vec3(-club.spin_bias * speed, 0.0f, club.accuracy * state.tuning.launch_side_spin_scale);
    state.swing = swing_state{};
    state.mode = game_mode::following_shot;
    clear_flight_path(state);
    append_flight_path_point(state, state.ball.position);
    ++state.stroke_count;
    award_skill_xp(state, golf_swing_skill_id(), 25);
    push_club_audio_event(state, audio_event_type::club_hit);
}

club_stats selected_club_stats(const game_state& state) {
    if (state.selected_club >= state.tuning.clubs.size()) {
        return club_stats{};
    }
    return state.tuning.clubs[state.selected_club].stats;
}

club_stats effective_selected_club_stats(const game_state& state) {
    return apply_cigarette_effect(selected_club_stats(state), state.cigarette_effect);
}

void place_player_near_ball(game_state& state, frame_profile* profile = nullptr) {
    const glm::vec3 forward = aim_direction(state.aim_angle);
    state.player.position = state.ball.position - forward * state.tuning.player_stand_off_distance;
    state.player.position.y = terrain_height_at(state.tuning, state.player.position, profile);
    state.player.yaw = state.aim_angle;
    state.cart.yaw = state.player.yaw;
}

void enter_cart_mode(game_state& state) {
    if (state.cart.active) {
        return;
    }

    state.cart.active = true;
    state.cart.velocity = std::max(0.0f, state.cart.velocity);
    state.cart.yaw = state.player.yaw;
    state.cart.drift_timer = 0.0f;
    push_audio_event(state, audio_event_type::cart_start);
}

void exit_cart_mode(game_state& state) {
    state.cart.active = false;
    state.cart.velocity = 0.0f;
    state.cart.drift_timer = 0.0f;
    state.cart.yaw = state.player.yaw;
}

void update_cart(game_state& state, const input_state& input, const float dt, frame_profile* profile) {
    if (!input.left_shift.is_down) {
        exit_cart_mode(state);
        return;
    }

    enter_cart_mode(state);
    const glm::vec3 position_before = state.player.position;

    if (input.space.pressed) {
        state.cart.drift_timer = state.tuning.cart.drift_duration;
        state.cart.velocity = std::max(state.cart.velocity,
                                       state.tuning.cart.speed * state.tuning.cart.drift_speed_boost);
        push_audio_event(state, audio_event_type::cart_drift);
    }

    if (state.cart.drift_timer > 0.0f) {
        state.cart.drift_timer = std::max(0.0f, state.cart.drift_timer - dt);
    }

    const bool drifting = state.cart.drift_timer > 0.0f;
    const bool on_road = cart_has_road_bonus(state);
    const float road_control = on_road ? 1.12f : (state.hub.in_hub ? 0.78f : 1.0f);
    const float turn_rate = (drifting ? state.tuning.cart.drift_turn_rate : state.tuning.cart.turn_rate) * road_control;
    if (input.left.is_down) {
        state.cart.yaw += turn_rate * dt;
    }
    if (input.right.is_down) {
        state.cart.yaw -= turn_rate * dt;
    }

    const float road_speed = on_road ? 1.32f : (state.hub.in_hub ? 0.72f : 1.0f);
    const float target_speed = state.tuning.cart.speed *
        (drifting ? state.tuning.cart.drift_speed_boost : 1.0f) * road_speed;
    const float damping = (drifting ? state.tuning.cart.drift_damping : state.tuning.cart.normal_damping) * road_control;
    const float blend = 1.0f - std::exp(-std::max(0.0f, damping) * dt);
    state.cart.velocity += (target_speed - state.cart.velocity) * blend;

    const glm::vec3 forward = aim_direction(state.cart.yaw);
    state.player.position += forward * state.cart.velocity * dt;
    state.player.position.y = terrain_height_at(state.tuning, state.player.position, profile);
    state.player.yaw = state.cart.yaw;

    const glm::vec3 moved = state.player.position - position_before;
    const float distance_meters = glm::length(glm::vec3(moved.x, 0.0f, moved.z)) * state.tuning.scale.meters_per_world_unit;
    if (on_road && distance_meters > 0.0f) {
        state.cart_drive_meter_remainder += distance_meters;
        const int earned_cart_xp = static_cast<int>(std::floor(state.cart_drive_meter_remainder / 20.0f));
        if (earned_cart_xp > 0) {
            award_skill_xp(state, cart_driving_skill_id(), earned_cart_xp);
            state.cart_drive_meter_remainder -= static_cast<float>(earned_cart_xp) * 20.0f;
        }

        if (drifting) {
            state.cart_drift_meter_remainder += distance_meters;
            const int earned_drift_xp = static_cast<int>(std::floor(state.cart_drift_meter_remainder / 8.0f));
            if (earned_drift_xp > 0) {
                award_skill_xp(state, drifting_skill_id(), earned_drift_xp);
                state.cart_drift_meter_remainder -= static_cast<float>(earned_drift_xp) * 8.0f;
            }
        }
    }
}

void apply_ground_roll_friction(game_state& state,
                                const terrain_sample& terrain,
                                const float roll_friction_scale,
                                const float dt) {
    if (terrain.material == terrain_material::water) {
        return;
    }
    if (ball_support_distance(state.ball, terrain) > state.ball.radius + 0.001f) {
        return;
    }

    const glm::vec3 normal = glm::length(terrain.normal) > 0.00001f
        ? glm::normalize(terrain.normal)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    const float normal_speed = glm::dot(state.ball.velocity, normal);
    if (normal_speed > state.tuning.ground_settle_speed) {
        return;
    }

    const glm::vec3 tangent_velocity = state.ball.velocity - normal * normal_speed;
    const float speed = glm::length(tangent_velocity);
    if (speed <= 0.0f) {
        state.ball.velocity = glm::vec3(0.0f);
        return;
    }

    const float speed_after_friction = std::max(0.0f,
                                                speed - state.tuning.ground_roll_friction *
                                                std::max(0.0f, roll_friction_scale) * dt);
    state.ball.velocity = tangent_velocity * (speed_after_friction / speed);
}

void update_swing(game_state& state, const input_state& input, const float dt) {
    if (state.swing.phase == swing_phase::timing) {
        state.swing.elapsed += dt;
        state.swing.power = sample_swing_power(state.swing.elapsed * effective_selected_club_stats(state).timing_speed);
    }

    if (!input.space.pressed) {
        return;
    }

    if (state.swing.phase == swing_phase::idle) {
        state.swing.phase = swing_phase::timing;
        state.swing.elapsed = 0.0f;
        state.swing.power = 0.0f;
        push_audio_event(state, audio_event_type::swing_start);
        return;
    }

    launch_ball(state);
}

void update_walking(game_state& state, const input_state& input, const float dt, frame_profile* profile) {
    if (cart_unlocked(state.save) && (input.left_shift.is_down || state.cart.active)) {
        update_cart(state, input, dt, profile);
        return;
    }

    const glm::vec3 position_before = state.player.position;

    if (input.left.is_down) {
        state.player.yaw += state.tuning.player_turn_rate * dt;
    }

    if (input.right.is_down) {
        state.player.yaw -= state.tuning.player_turn_rate * dt;
    }

    const glm::vec3 forward = aim_direction(state.player.yaw);
    if (input.up.is_down) {
        state.player.position += forward * state.tuning.player_walk_speed * dt;
    }

    if (input.down.is_down) {
        state.player.position -= forward * state.tuning.player_walk_speed * dt;
    }

    state.player.position.y = terrain_height_at(state.tuning, state.player.position, profile);

    const glm::vec3 walk_delta = state.player.position - position_before;
    const glm::vec3 horizontal_delta(walk_delta.x, 0.0f, walk_delta.z);
    state.fitness_walk_meter_remainder += glm::length(horizontal_delta) * state.tuning.scale.meters_per_world_unit;
    const int earned_fitness_xp = static_cast<int>(std::floor(state.fitness_walk_meter_remainder / 10.0f));
    if (earned_fitness_xp > 0) {
        award_skill_xp(state, fitness_skill_id(), earned_fitness_xp);
        state.fitness_walk_meter_remainder -= static_cast<float>(earned_fitness_xp) * 10.0f;
    }

    if (state.hub.available && state.hub.in_hub && input.space.pressed) {
        const int collectible_index = nearby_collectible_index(state);
        if (collectible_index >= 0) {
            const bool collected = apply_collectible_reward(state.save,
                                                            state.hub.world.collectibles[static_cast<std::size_t>(collectible_index)],
                                                            static_cast<int>(state.round.current_hole_index));
            if (collected) {
                mark_terrain_render_dirty(state);
            }
            return;
        }

        const int start_index = nearby_hole_start_index(state);
        if (start_index >= 0) {
            start_hub_hole(state, static_cast<std::size_t>(state.hub.world.hole_starts[static_cast<std::size_t>(start_index)].hole_index));
        }
        return;
    }

    if (input.space.pressed && can_interact_with_ball(state)) {
        state.mode = game_mode::aiming;
        state.aim_angle = state.player.yaw;
        state.swing = swing_state{};
    }
}

void update_aiming(game_state& state, const input_state& input, const float dt, frame_profile* profile) {
    if (input.left.is_down) {
        state.aim_angle += state.tuning.aim_turn_rate * dt;
    }

    if (input.right.is_down) {
        state.aim_angle -= state.tuning.aim_turn_rate * dt;
    }

    state.player.yaw = state.aim_angle;

    if (input.up.pressed) {
        select_previous_club(state);
    }

    if (input.down.pressed) {
        select_next_club(state);
    }

    if (input.space.pressed) {
        state.player.position = address_player_position(state, profile);
        state.mode = game_mode::addressing;
        state.swing = swing_state{};
    }
}

void update_addressing(game_state& state, const input_state& input, const float dt) {
    if (input.up.pressed) {
        select_previous_club(state);
    }

    if (input.down.pressed) {
        select_next_club(state);
    }

    update_swing(state, input, dt);
}

void step_ball(game_state& state, const float dt, frame_profile* profile) {
    if (!ball_is_moving(state.ball, state.tuning)) {
        const terrain_sample terrain = terrain_sample_at(state.tuning, state.ball.position, profile);
        const bool in_water = terrain.material == terrain_material::water;
        const float restitution = in_water ? state.tuning.water_restitution : state.tuning.ground_restitution;
        const float friction = in_water ? state.tuning.water_friction : state.tuning.ground_friction;
        state.ball = resolve_terrain_collision(state.ball,
                                               terrain,
                                               restitution,
                                               friction);
        state.ball.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
        state.ball.spin = glm::vec3(0.0f, 0.0f, 0.0f);
        return;
    }

    const club_stats launched_club = selected_club_stats(state);
    const wind_state wind = sample_wind(state.tuning.wind_seed, state.hole_time, state.tuning.wind);
    const terrain_sample terrain_before = terrain_sample_at(state.tuning, state.ball.position, profile);
    const bool was_airborne = ball_support_distance(state.ball, terrain_before) > state.ball.radius + 0.001f;
    const bool water_zone = terrain_before.material == terrain_material::water;
    const float water_depth = std::max(0.0f, state.tuning.zone_tuning.water_depth);
    // Water surface is inferred from the deformed mesh depth.
    const float water_surface = terrain_before.point.y + water_depth;
    const bool water_volume = water_zone && (state.ball.position.y - state.ball.radius) <= water_surface;

    physics_tuning physics = state.tuning.physics;
    if (water_volume) {
        physics.drag_coeff += physics.water_drag_coeff;
        physics.spin_decay += physics.water_spin_decay;
    }

    state.ball = step(state.ball, wind, dt, physics);
    const terrain_sample terrain = terrain_sample_at(state.tuning, state.ball.position, &terrain_before, profile);
    const bool in_water = terrain.material == terrain_material::water;
    const float restitution = in_water ? state.tuning.water_restitution : state.tuning.ground_restitution;
    const float friction = in_water
        ? state.tuning.water_friction
        : state.tuning.ground_friction * std::max(0.0f, launched_club.roll_friction_scale);
    state.ball = resolve_terrain_collision(state.ball,
                                           terrain,
                                           restitution,
                                           friction);
    const bool is_grounded = ball_support_distance(state.ball, terrain) <= state.ball.radius + 0.001f;
    if (was_airborne && is_grounded) {
        push_ball_land_audio_event(state, terrain.material);
    }

    refresh_static_anchor_cache(state, profile);
    const ball_state before_tree_collision = state.ball;
    state.ball = resolve_tree_collisions(state.ball,
                                         state.static_anchors.tree_bodies,
                                         state.tuning.tree_restitution,
                                         state.tuning.tree_friction);
    if (glm::length(state.ball.velocity - before_tree_collision.velocity) > 0.01f ||
        glm::length(state.ball.position - before_tree_collision.position) > 0.001f) {
        push_audio_event(state, audio_event_type::ball_tree_hit);
    }
    apply_ground_roll_friction(state, terrain, launched_club.roll_friction_scale, dt);
}

void reset_transient_hole_state(game_state& state) {
    state.ball.radius = state.tuning.scale.ball_physics_radius_meters;
    state.ball.mass = state.tuning.scale.ball_mass_kg;
    state.ball.position = ball_rest_position(state.tuning, state.tuning.course.tee_position, state.ball.radius);
    state.ball.velocity = glm::vec3(0.0f);
    state.ball.spin = glm::vec3(0.0f);
    state.shot_camera_position = glm::vec3(0.0f);
    state.swing = swing_state{};
    state.cart = cart_state{};
    clear_emote(state);
    state.cigarette_effect = cigarette_effect_state{};
    state.mode = game_mode::walking;
    state.stroke_count = 0;
    state.hole_time = 0.0f;
    state.skills_panel_active = false;
    state.fitness_walk_meter_remainder = 0.0f;
    state.cart_drive_meter_remainder = 0.0f;
    state.cart_drift_meter_remainder = 0.0f;
    clear_flight_path(state);
    state.aim_angle = aim_angle_towards(state.ball.position, pin_anchor_position(state));
    place_player_near_ball(state);
    input_state input;
    update_walk_overlays(state, input);
}

bool reset_transient_hub_state(game_state& state, const glm::vec3& spawn_position) {
    if (!apply_course_world_to_tuning(state)) {
        return false;
    }
    mark_terrain_render_dirty(state);
    terrain_sample terrain = terrain_sample_at(state.tuning, spawn_position);
    terrain.point.x = spawn_position.x;
    terrain.point.z = spawn_position.z;
    state.ball.radius = state.tuning.scale.ball_physics_radius_meters;
    state.ball.mass = state.tuning.scale.ball_mass_kg;
    state.ball.position = terrain.point + terrain.normal * state.ball.radius;
    state.ball.velocity = glm::vec3(0.0f);
    state.ball.spin = glm::vec3(0.0f);
    state.shot_camera_position = glm::vec3(0.0f);
    state.swing = swing_state{};
    state.cart = cart_state{};
    clear_emote(state);
    state.cigarette_effect = cigarette_effect_state{};
    state.mode = game_mode::walking;
    state.stroke_count = 0;
    state.hole_time = 0.0f;
    state.skills_panel_active = false;
    state.fitness_walk_meter_remainder = 0.0f;
    state.cart_drive_meter_remainder = 0.0f;
    state.cart_drift_meter_remainder = 0.0f;
    clear_flight_path(state);
    state.player.position = terrain.point;
    state.player.yaw = state.hub.world.hole_starts.empty()
        ? 0.0f
        : aim_angle_towards(state.player.position, state.hub.world.hole_starts.front().position);
    state.cart.yaw = state.player.yaw;
    state.aim_angle = state.player.yaw;
    state.hub.in_hub = true;
    input_state input;
    update_walk_overlays(state, input);
    return true;
}

void return_to_hub(game_state& state, const glm::vec3& return_position) {
    reset_transient_hub_state(state, return_position);
}
}

game_state make_initial_game_state() {
    return make_initial_game_state(resolve_asset_root(""));
}

static_anchor_cache build_static_anchor_cache(const game_tuning& tuning,
                                              const std::vector<course_hub_hole_marker>& hub_markers,
                                              const std::uint64_t revision,
                                              frame_profile* profile) {
    static_anchor_cache cache;
    cache.valid = true;
    cache.revision = revision;
    cache.tee_anchor = terrain_anchor_position(tuning, tuning.course.tee_position, profile);
    cache.pin_anchor = terrain_anchor_position(tuning, tuning.course.pin_position, profile);
    cache.tree_bodies = anchored_tree_bodies(tuning, profile);
    cache.hub_tee_markers = anchored_hub_marker_positions(tuning, hub_markers, hub_marker_kind::tee, profile);
    cache.hub_pin_markers = anchored_hub_marker_positions(tuning, hub_markers, hub_marker_kind::pin, profile);
    cache.hub_start_markers = anchored_hub_marker_positions(tuning, hub_markers, hub_marker_kind::start, profile);
    return cache;
}

bool static_anchor_cache_is_current(const game_state& state) {
    return state.static_anchors.valid && state.static_anchors.revision == state.terrain_render_revision;
}

void refresh_static_anchor_cache(game_state& state, frame_profile* profile) {
    if (static_anchor_cache_is_current(state)) {
        return;
    }
    state.static_anchors = build_static_anchor_cache(state.tuning,
                                                     state.hub.hole_markers,
                                                     state.terrain_render_revision,
                                                     profile);
}

void mark_terrain_render_dirty(game_state& state) {
    ++state.terrain_render_revision;
    refresh_static_anchor_cache(state);
}

glm::vec3 pin_anchor_position(const game_state& state) {
    if (static_anchor_cache_is_current(state)) {
        return state.static_anchors.pin_anchor;
    }
    return terrain_anchor_position(state.tuning, state.tuning.course.pin_position);
}

void refresh_unlocked_clubs(game_state& state) {
    if (state.club_catalog.empty()) {
        state.club_catalog = state.tuning.clubs;
    }

    std::string selected_id;
    if (state.selected_club < state.tuning.clubs.size()) {
        selected_id = state.tuning.clubs[state.selected_club].id;
    }

    std::vector<club_definition> available;
    for (const club_definition& club : state.club_catalog) {
        if (club_available(state.save, club)) {
            available.push_back(club);
        }
    }

    if (available.empty()) {
        for (const club_definition& club : state.club_catalog) {
            if (club.id == "putter") {
                available.push_back(club);
                break;
            }
        }
    }

    state.tuning.clubs = available;
    state.selected_club = 0;
    if (!selected_id.empty()) {
        for (std::size_t i = 0; i < state.tuning.clubs.size(); ++i) {
            if (state.tuning.clubs[i].id == selected_id) {
                state.selected_club = i;
                return;
            }
        }
    }
}

void award_skill_xp(game_state& state, const std::string& skill_id, const int amount, const xp_drop_policy policy) {
    const add_skill_xp_result result = add_skill_xp(state.save.skills, skill_id, amount);
    if (policy == xp_drop_policy::hidden || result.applied_xp <= 0 || skill_id.empty()) {
        return;
    }

    int visible_xp = result.applied_xp;
    int& pending_xp = state.pending_xp_drop_amounts[skill_id];
    if (visible_xp < min_visible_xp_drop) {
        pending_xp += visible_xp;
        if (pending_xp < min_visible_xp_drop) {
            return;
        }
        visible_xp = pending_xp;
        pending_xp = 0;
    } else if (pending_xp > 0) {
        visible_xp += pending_xp;
        pending_xp = 0;
    }

    emit_xp_drop(state, skill_id, visible_xp);
}

void update_xp_drops(game_state& state, const float dt) {
    const float clamped_dt = std::max(0.0f, dt);
    for (xp_drop& drop : state.xp_drops) {
        drop.age += clamped_dt;
    }

    state.xp_drops.erase(std::remove_if(state.xp_drops.begin(),
                                        state.xp_drops.end(),
                                        [](const xp_drop& drop) {
                                            return drop.age >= drop.lifetime;
                                        }),
                         state.xp_drops.end());
}

game_state make_initial_game_state(const std::string& asset_root) {
    game_state state;
    state.asset_root = asset_root;
    std::vector<course_definition> courses = load_courses_from_directory((std::filesystem::path(asset_root) / "courses").string());
    state.active_course = default_course_definition(courses);
    state.round = start_course(state.active_course);
    state.save.current_course_id = state.active_course.id;
    state.save.current_hole_index = 0;
    state.tuning = default_game_tuning(asset_root);
    mark_terrain_render_dirty(state);
    state.club_catalog = state.tuning.clubs;
    refresh_unlocked_clubs(state);
    reset_transient_hole_state(state);
    return state;
}

void retee_ball(game_state& state) {
    const int strokes = state.stroke_count;
    reset_transient_hole_state(state);
    state.stroke_count = strokes;
}

bool start_game_course(game_state& state, const course_definition& course) {
    const round_state next_round = start_course(course);
    if (next_round.finished) {
        return false;
    }

    state.active_course = course;
    state.round = next_round;
    state.save.current_course_id = course.id;
    state.save.current_hole_index = 0;

    state.hub = course_hub_state{};
    const std::string world_path = course_world_file_path(state.asset_root, course);
    if (!world_path.empty()) {
        const std::optional<course_world_definition> world = load_course_world_from_file(world_path, course);
        if (world) {
            state.hub.available = true;
            state.hub.in_hub = true;
            state.hub.active_hole_index = 0;
            state.hub.return_position = world->spawn.position;
            state.hub.world = *world;
            if (reset_transient_hub_state(state, state.hub.world.spawn.position)) {
                return true;
            }
            state.hub = course_hub_state{};
        }
    }

    if (!load_hole_runtime(state.tuning, course, 0, state.asset_root)) {
        // state.hub was cleared above; invalidate so cached hub markers match it.
        mark_terrain_render_dirty(state);
        return false;
    }
    mark_terrain_render_dirty(state);

    reset_transient_hole_state(state);
    return true;
}

bool start_hub_hole(game_state& state, const std::size_t hole_index) {
    if (!state.hub.available || hole_index >= state.active_course.holes.size()) {
        return false;
    }
    if (!load_hole_runtime(state.tuning, state.active_course, hole_index, state.asset_root)) {
        return false;
    }
    mark_terrain_render_dirty(state);

    state.round.current_hole_index = hole_index;
    state.save.current_course_id = state.active_course.id;
    state.save.current_hole_index = static_cast<int>(hole_index);
    state.hub.in_hub = false;
    state.hub.active_hole_index = hole_index;
    state.hub.return_position = state.hub.world.spawn.position;
    for (const course_world_hole_start& start : state.hub.world.hole_starts) {
        if (start.hole_index == static_cast<int>(hole_index)) {
            state.hub.return_position = start.return_position;
            break;
        }
    }
    reset_transient_hole_state(state);
    return true;
}

bool complete_current_hole(game_state& state) {
    push_audio_event(state, audio_event_type::hole_complete);
    const std::size_t completed_index = state.round.current_hole_index;
    state.save.current_course_id = state.active_course.id;
    state.save.hole_scores[static_cast<int>(completed_index)] = state.stroke_count;
    const bool has_next = complete_hole(state.round, state.stroke_count);
    state.save.current_hole_index = static_cast<int>(state.round.current_hole_index);
    if (state.round.finished) {
        if (!state.active_course.id.empty() && !contains_string(state.save.completed_course_ids, state.active_course.id)) {
            state.save.completed_course_ids.push_back(state.active_course.id);
        }
        return true;
    }

    if (state.hub.available) {
        return_to_hub(state, state.hub.return_position);
        return true;
    }

    if (!has_next || state.round.current_hole_index == completed_index) {
        return false;
    }

    if (!load_hole_runtime(state.tuning, state.active_course, state.round.current_hole_index, state.asset_root)) {
        return false;
    }
    mark_terrain_render_dirty(state);

    reset_transient_hole_state(state);
    return true;
}

bool ball_is_in_cup(const game_state& state) {
    const glm::vec3 pin_anchor = pin_anchor_position(state);
    const glm::vec3 delta = state.ball.position - pin_anchor;
    const glm::vec3 horizontal_delta(delta.x, 0.0f, delta.z);
    return glm::length(horizontal_delta) <= effective_cup_radius(state.tuning);
}

void update_game(game_state& state, const input_state& input, const float dt, frame_profile* profile) {
    state.audio_events.clear();
    refresh_static_anchor_cache(state, profile);
    const float clamped_dt = std::max(0.0f, std::min(dt, 0.05f));
    update_xp_drops(state, clamped_dt);
    if (state.round.finished) {
        update_walk_overlays(state, input);
        return;
    }

    state.hole_time += clamped_dt;
    update_emote_state(state, input, clamped_dt);

    if (input.retee.pressed) {
        retee_ball(state);
        update_walk_overlays(state, input);
        return;
    }

    if (should_cancel_shot_setup(state.mode, input)) {
        exit_cart_mode(state);
        state.mode = game_mode::walking;
        state.swing = swing_state{};
        update_walk_overlays(state, input);
        return;
    }

    if (state.mode == game_mode::walking) {
        update_walking(state, input, clamped_dt, profile);
    } else if (state.mode == game_mode::aiming) {
        update_aiming(state, input, clamped_dt, profile);
    } else if (state.mode == game_mode::addressing) {
        update_addressing(state, input, clamped_dt);
    }

    if (state.mode != game_mode::walking) {
        exit_cart_mode(state);
    }

    if (state.mode == game_mode::following_shot || ball_is_moving(state.ball, state.tuning)) {
        state.mode = game_mode::following_shot;
        const glm::vec3 previous_ball_position = state.ball.position;
        step_ball(state, clamped_dt, profile);
        append_flight_path_point(state, state.ball.position);

        if (complete_if_ball_reached_cup(state, previous_ball_position)) {
            update_walk_overlays(state, input);
            return;
        }

        if (!ball_is_moving(state.ball, state.tuning)) {
            state.ball.velocity = glm::vec3(0.0f);
            state.ball.spin = glm::vec3(0.0f);
            state.mode = game_mode::walking;
            clear_flight_path(state);
            if (ball_is_in_cup(state)) {
                sink_ball_in_cup(state);
                complete_current_hole(state);
                update_walk_overlays(state, input);
                return;
            }
        }
    }

    if (state.mode == game_mode::walking &&
        !state.round.finished &&
        !ball_is_moving(state.ball, state.tuning) &&
        ball_is_in_cup(state)) {
        sink_ball_in_cup(state);
        complete_current_hole(state);
        update_walk_overlays(state, input);
        return;
    }

    update_walk_overlays(state, input);
}

bool ball_is_moving(const ball_state& ball, const game_tuning& tuning) {
    const terrain_sample terrain = terrain_sample_at(tuning, ball.position);
    return glm::length(ball.velocity) > tuning.stop_speed
        || ball_support_distance(ball, terrain) > ball.radius + 0.001f;
}

bool ball_is_moving(const ball_state& ball) {
    return ball_is_moving(ball, default_game_tuning());
}

bool can_interact_with_ball(const game_state& state) {
    if (state.hub.available && state.hub.in_hub) {
        return false;
    }
    const glm::vec3 delta = state.player.position - state.ball.position;
    const glm::vec3 horizontal_delta(delta.x, 0.0f, delta.z);
    return glm::length(horizontal_delta) <= state.tuning.ball_interact_radius
        && !ball_is_moving(state.ball, state.tuning);
}

int nearby_hole_start_index(const game_state& state) {
    if (!state.hub.available || !state.hub.in_hub) {
        return -1;
    }

    int best_index = -1;
    float best_distance = 0.0f;
    for (std::size_t i = 0; i < state.hub.world.hole_starts.size(); ++i) {
        const course_world_hole_start& start = state.hub.world.hole_starts[i];
        const glm::vec3 delta = state.player.position - start.position;
        const glm::vec3 horizontal_delta(delta.x, 0.0f, delta.z);
        const float distance = glm::length(horizontal_delta);
        if (distance > start.interaction_radius) {
            continue;
        }
        if (best_index < 0 || distance < best_distance) {
            best_index = static_cast<int>(i);
            best_distance = distance;
        }
    }
    return best_index;
}

bool can_interact_with_hole_start(const game_state& state) {
    return nearby_hole_start_index(state) >= 0;
}

int nearby_collectible_index(const game_state& state) {
    if (!state.hub.available || !state.hub.in_hub) {
        return -1;
    }

    int best_index = -1;
    float best_distance = 0.0f;
    const int current_hole_index = static_cast<int>(state.round.current_hole_index);
    for (std::size_t i = 0; i < state.hub.world.collectibles.size(); ++i) {
        const course_world_collectible& collectible = state.hub.world.collectibles[i];
        if (!collectible_available(state.save, collectible, current_hole_index)) {
            continue;
        }

        const glm::vec3 delta = state.player.position - collectible.position;
        const glm::vec3 horizontal_delta(delta.x, 0.0f, delta.z);
        const float distance = glm::length(horizontal_delta);
        if (distance > collectible.interaction_radius) {
            continue;
        }
        if (best_index < 0 || distance < best_distance) {
            best_index = static_cast<int>(i);
            best_distance = distance;
        }
    }
    return best_index;
}

bool can_interact_with_collectible(const game_state& state) {
    return nearby_collectible_index(state) >= 0;
}

float cart_road_distance(const game_state& state) {
    if (!state.hub.available || !state.hub.in_hub || state.hub.world.cart_roads.empty()) {
        return std::numeric_limits<float>::infinity();
    }

    float nearest = std::numeric_limits<float>::infinity();
    for (const course_world_path& road : state.hub.world.cart_roads) {
        if (road.polyline.empty()) {
            continue;
        }
        if (road.polyline.size() == 1) {
            const glm::vec3 delta(state.player.position.x - road.polyline.front().x,
                                  0.0f,
                                  state.player.position.z - road.polyline.front().z);
            nearest = std::min(nearest, glm::length(delta));
            continue;
        }
        for (std::size_t i = 0; i + 1 < road.polyline.size(); ++i) {
            const glm::vec3 a = road.polyline[i];
            const glm::vec3 b = road.polyline[i + 1];
            const glm::vec3 ab(b.x - a.x, 0.0f, b.z - a.z);
            const glm::vec3 ap(state.player.position.x - a.x, 0.0f, state.player.position.z - a.z);
            const float denom = glm::dot(ab, ab);
            const float t = denom <= 0.0001f ? 0.0f : std::clamp(glm::dot(ap, ab) / denom, 0.0f, 1.0f);
            const glm::vec3 closest = a + ab * t;
            const glm::vec3 delta(state.player.position.x - closest.x, 0.0f, state.player.position.z - closest.z);
            nearest = std::min(nearest, glm::length(delta));
        }
    }
    return nearest;
}

bool cart_has_road_bonus(const game_state& state) {
    if (!state.hub.available || !state.hub.in_hub) {
        return false;
    }
    float influence = 4.0f;
    for (const course_world_path& road : state.hub.world.cart_roads) {
        influence = std::max(influence, road.width * 0.5f + 2.5f);
    }
    return cart_road_distance(state) <= influence;
}

bool rangefinder_should_show(const game_mode mode, const input_state& input) {
    return mode == game_mode::walking && input.shift.is_down && !input.left_shift.is_down;
}

bool course_map_should_show(const game_mode mode, const input_state& input) {
    return mode == game_mode::walking && input.enter.is_down;
}

bool scorecard_should_show(const game_mode mode, const input_state& input) {
    return mode == game_mode::walking && input.tab.is_down;
}

bool should_cancel_shot_setup(const game_mode mode, const input_state& input) {
    return (mode == game_mode::aiming || mode == game_mode::addressing)
        && (input.backspace.pressed || input.escape.pressed);
}

glm::vec3 follow_camera_target(const glm::vec3& ball_position) {
    return ball_position + glm::vec3(0.0f, 0.25f, 0.0f);
}

float compute_rangefinder_distance_meters(const glm::vec3& player_position,
                                          const glm::vec3& pin_anchor,
                                          const float meters_per_world_unit) {
    const glm::vec3 delta = pin_anchor - player_position;
    const glm::vec3 horizontal_delta(delta.x, 0.0f, delta.z);
    return glm::length(horizontal_delta) * meters_per_world_unit;
}

std::string format_rangefinder_distance(const float distance_meters) {
    const int rounded_meters = static_cast<int>(std::floor(std::max(0.0f, distance_meters) + 0.5f));
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%dM", rounded_meters);
    return std::string(buffer);
}
