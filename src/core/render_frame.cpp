#include "core/render_frame.h"

#include "core/render_online.h"

#include "game/course_session.h"
#include "game/mode_dispatch.h"
#include "game/net_types.h"
#include "game/progress_rules.h"
#include "game/group_round.h"
#include "game/scorecard.h"
#include "game/shot_simulation.h"
#include "game/text_ids.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace {
// The address view of `ball`: beside it on the player's side, looking along
// the aim. The follow camera holds this view of where the shot started.
camera_view address_view(const camera_tuning& camera, const glm::vec3& ball, const float aim_angle,
                         const float fov_degrees) {
    const glm::vec3 forward = yaw_direction(aim_angle);
    camera_view view;
    view.position = ball + yaw_left(forward) * camera.address_side_distance - forward * camera.address_back_distance +
        world_up * camera.address_eye_height;
    view.target = ball + forward * camera.address_look_distance + world_up * camera.address_look_height;
    view.fov_degrees = fov_degrees;
    return view;
}

// Where a full-power shot with the selected club would fly, ignoring drag,
// spin and wind, from the lie the ball is on; ends where it meets the terrain.
std::vector<glm::vec3> aim_preview_points(const game_state& game, frame_profile* profile) {
    std::vector<glm::vec3> points;
    if (game.selected_club >= game.clubs.size()) {
        return points;
    }
    const club_stats stats =
        lie_club_stats(game.clubs[game.selected_club].stats, lie_at(game.area, game.ball.position), game.tuning);
    const glm::vec3 velocity = launch_ball(game.ball.position, game.aim_angle, stats, 1.0f, game.tuning).velocity;
    const aim_preview_tuning& tuning = game.tuning.aim_preview;

    for (int i = 1; i <= tuning.max_points; ++i) {
        const float t = static_cast<float>(i) * tuning.step_seconds;
        glm::vec3 point = game.ball.position + velocity * t - world_up * (0.5f * gravity_meters_per_second2 * t * t);
        const float ground = terrain_height(game.area, point, profile);
        if (point.y < ground) {
            point.y = ground;
            points.push_back(point);
            break;
        }
        points.push_back(point);
    }
    return points;
}

controls_overlay_state controls_from_keys(const input_state& keys) {
    controls_overlay_state controls;
    controls.key_1_down = keys.key_1.is_down;
    controls.key_2_down = keys.key_2.is_down;
    controls.left_down = keys.left.is_down;
    controls.right_down = keys.right.is_down;
    controls.up_down = keys.up.is_down;
    controls.down_down = keys.down.is_down;
    controls.space_down = keys.space.is_down;
    controls.shift_down = keys.left_shift.is_down;
    controls.enter_down = keys.enter.is_down;
    controls.backspace_down = keys.backspace.is_down;
    controls.retee_down = keys.key_r.is_down;
    controls.group_down = keys.key_g.is_down;
    return controls;
}

std::vector<render_skill_progress> skill_rows(const save_data& save,
                                              const std::vector<skill_definition>& skills,
                                              const text_assets& text) {
    std::vector<render_skill_progress> rows;
    for (const skill_definition& skill : skills) {
        render_skill_progress row;
        row.label = lookup_text(text, skill_text_key(skill.id).c_str());
        row.xp = skill_xp(save.skills, skill.id);
        row.level = skill_level(row.xp);
        row.xp_to_next = xp_to_next_level(save.skills, skill.id);
        rows.push_back(row);
    }
    return rows;
}

std::vector<render_xp_drop> xp_drop_rows(const game_state& game, const std::vector<skill_definition>& skills) {
    std::vector<render_xp_drop> rows;
    const float lifetime = std::max(0.001f, game.tuning.xp_drops.lifetime_seconds);
    for (const xp_drop& drop : game.xp_drops) {
        const auto skill = std::find_if(skills.begin(), skills.end(),
                                        [&drop](const skill_definition& definition) { return definition.id == drop.skill_id; });
        render_xp_drop row;
        row.icon = skill_icon_from_name(skill != skills.end() ? skill->icon : std::string());
        row.xp = drop.xp;
        row.progress = drop.age / lifetime;
        rows.push_back(row);
    }
    return rows;
}

// Every hole's pin but the one being played, which is drawn as `pin_position`.
std::vector<glm::vec3> other_pin_markers(const game_state& game) {
    std::vector<glm::vec3> pins;
    const std::vector<glm::vec3>& all = game.static_anchors.hub_pin_markers;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!game.hole || i != game.round.current_hole_index) {
            pins.push_back(all[i]);
        }
    }
    return pins;
}

// What the course map shows: the course's holes, each numbered at its tee.
void add_course_map(render_data& data, const game_state& game, const text_assets& text) {
    data.course_map_low = game.area.holes_low;
    data.course_map_high = game.area.holes_high;
    for (const hole_sign& sign : game.area.signs) {
        if (sign.line_of_play.empty()) {
            continue;
        }
        const std::size_t number = course_hole_of_area_hole(game, sign.area_hole) + 1;
        data.map_holes.push_back(render_map_hole{
            format_text(text, text_course_map_hole_number, {{"hole", std::to_string(number)}}), sign.line_of_play.front()});
    }
}

void add_hub_markers(render_data& data, const game_state& game) {
    const static_anchor_cache& anchors = game.static_anchors;
    const std::vector<course_world_collectible>& collectibles = game.hub->world.collectibles;
    for (std::size_t i = 0; i < collectibles.size() && i < anchors.collectibles.size(); ++i) {
        if (collectible_available(active_progress(game), collectibles[i])) {
            data.collectible_markers.push_back(anchors.collectibles[i]);
        }
    }
}
}

camera_rig active_camera_rig(const game_state& game) {
    switch (game.mode) {
    case game_mode::walking:
        return camera_rig::walking;
    case game_mode::aiming:
        return camera_rig::aiming;
    case game_mode::addressing:
        return camera_rig::addressing;
    case game_mode::following_shot:
        return camera_rig::following_shot;
    }
    return camera_rig::walking;
}

camera_view live_camera_view(const game_state& game, const float fov_degrees) {
    const camera_tuning& camera = game.tuning.camera;
    switch (active_camera_rig(game)) {
    case camera_rig::walking: {
        camera_view view;
        view.position = game.player.position + world_up * camera.walking_eye_height;
        view.target = view.position + yaw_direction(game.player.yaw) * camera.walking_look_distance;
        view.fov_degrees = fov_degrees;
        return view;
    }
    case camera_rig::aiming: {
        const glm::vec3 forward = yaw_direction(game.aim_angle);
        camera_view view;
        view.position = game.ball.position - forward * camera.aiming_back_distance + world_up * camera.aiming_eye_height;
        view.target = game.ball.position + forward * camera.aiming_look_distance + world_up * camera.aiming_look_height;
        view.fov_degrees = fov_degrees;
        return view;
    }
    case camera_rig::addressing:
        return address_view(camera, game.ball.position, game.aim_angle, fov_degrees);
    case camera_rig::following_shot: {
        camera_view view = address_view(camera, game.shot_start_position, game.aim_angle, fov_degrees);
        view.target = game.ball.position + world_up * camera.follow_look_height;
        return view;
    }
    }
    return camera_view{};
}

namespace {
// "OFFLINE", or the room online, so the two progress stores are never
// confused. Without a connection or a room, it says the game is reconnecting.
std::string mode_label(const game_state& game, const text_assets& text) {
    if (!is_online(game)) {
        return lookup_text(text, text_hud_mode_offline);
    }
    const std::optional<online_room>& room = game.online.room;
    if (game.online.status != net_status::connected || !room) {
        return lookup_text(text, text_hud_mode_reconnecting);
    }
    return format_text(text, text_hud_mode_online,
                       {{"course", game.course.name},
                        {"room", std::to_string(room->room_id)},
                        {"players", std::to_string(room->player_count)},
                        {"capacity", std::to_string(game.tuning.server.room_capacity)}});
}
}

glm::vec3 drawn_ball_center(const glm::vec3& physics_center, const world_scale_tuning& scale) {
    return physics_center + world_up * (scale.ball_visual_radius_meters - scale.ball_physics_radius_meters);
}

render_data make_render_data(const game_state& game,
                             const input_state& keys,
                             const text_assets& text,
                             const std::vector<skill_definition>& skills,
                             const render_meshes& meshes,
                             const float fov_degrees,
                             frame_profile* profile) {
    render_data data;
    const camera_view view = live_camera_view(game, fov_degrees);
    data.camera_position = view.position;
    data.camera_target = view.target;
    data.camera_fov_degrees = view.fov_degrees;
    data.viewmodel_fov_degrees = game.tuning.camera.viewmodel_fov_degrees;

    data.terrain_mesh = meshes.terrain;
    data.material_overlay_mesh = meshes.material_overlay;
    data.hole_signs = meshes.hole_signs;
    data.fences = meshes.fences;
    data.water = meshes.water;
    data.backdrop = &game.course.backdrop;
    data.trees = &game.static_anchors.trees;
    data.trees_revision = game.static_anchors.revision;
    data.area_center = game.area.center;
    data.area_extent = game.area.extent;

    const world_scale_tuning& scale = game.tuning.scale;
    data.player_position = game.player.position;
    data.ball_position = drawn_ball_center(game.ball.position, scale);
    data.ball_visual_radius_meters = scale.ball_visual_radius_meters;
    data.cup_radius_meters = scale.cup_radius_meters;
    data.pin_visual_height_meters = scale.pin_visual_height_meters;
    data.show_ball = game.hole.has_value();
    data.show_hole = game.hole.has_value();
    data.tee_position = game.static_anchors.tee_anchor;
    data.tee_boxes = &game.area.tee_boxes;
    data.pin_position = game.static_anchors.pin_anchor;
    data.pin_markers = other_pin_markers(game);
    if (in_hub(game)) {
        add_hub_markers(data, game);
    }
    if (in_hub(game) && game.cup_ball) {
        // Sitting in the cup: its centre at the rim, the top showing.
        data.show_ball = true;
        data.ball_position = *game.cup_ball;
    }

    const bool shot_setup = game.mode == game_mode::aiming || game.mode == game_mode::addressing;
    data.show_aim_indicator = shot_setup;
    if (game.mode == game_mode::aiming) {
        data.aim_arc_points = aim_preview_points(game, profile);
    }
    data.aim_angle = game.aim_angle;
    data.show_swing_club = game.mode == game_mode::addressing;
    data.swing_power = game.swing.power;

    data.show_flight_path = game.mode == game_mode::following_shot && !game.flight_path_points.empty();
    data.flight_path_points = &game.flight_path_points;
    data.flight_path_color = game.tuning.flight_path.color;
    data.flight_path_alpha = game.tuning.flight_path.alpha;
    data.flight_path_width = game.tuning.flight_path.line_width;

    data.cart_active = game.cart.active;
    data.cart_drifting = game.cart.drift_timer > 0.0f;
    data.cart_speed_fraction = std::abs(game.cart.velocity) / std::max(0.001f, fastest_cart_speed(game.tuning.cart));
    data.smoke_emote_active = game.smoke_emote.active;
    data.smoke_emote_elapsed = game.smoke_emote.elapsed;
    data.beer_emote_active = game.beer_emote.active;
    data.beer_emote_elapsed = game.beer_emote.elapsed;

    data.mode_label = mode_label(game, text);
    data.show_interact_prompt = game.mode == game_mode::walking &&
        (can_interact_with_ball(game) || cup_ball_in_reach(game) || nearby_hole_start(game) ||
         nearby_collectible(game));
    data.show_power_meter = shot_setup;
    data.stroke_count = game.stroke_count;
    if (game.selected_club < game.clubs.size()) {
        data.selected_club_label = game.clubs[game.selected_club].label;
    }
    data.show_rangefinder = game.rangefinder_active;
    if (data.show_rangefinder) {
        data.rangefinder_target = game.hole ? data.pin_position : game.static_anchors.hub_pin_markers[game.round.current_hole_index];
        data.rangefinder_label = format_text(
            text, text_hud_rangefinder, {{"meters", std::to_string(rounded_rangefinder_meters(game.rangefinder_distance_meters))}});
    }
    data.show_course_map = game.course_map_active;
    if (data.show_course_map) {
        add_course_map(data, game, text);
    }
    data.show_scorecard = game.scorecard_active;
    data.show_course_results = round_results_shown(game);
    if (data.show_scorecard || data.show_course_results) {
        data.scorecard = build_scorecard_data(game, text.strings);
    }
    data.show_skills_panel = game.skills_panel_active;
    if (data.show_skills_panel) {
        data.skills = skill_rows(active_progress(game), skills, text);
    }
    if (!data.show_course_results) {
        data.xp_drops = xp_drop_rows(game, skills);
    }
    data.controls = controls_from_keys(keys);
    add_online_render_data(data, game, text);
    return data;
}
