#include "core/render_frame.h"

#include "core/render_online.h"

#include <glm/trigonometric.hpp>
#include "game/address_play.h"
#include "game/area_solids.h"

#include "game/golf_bag.h"
#include "game/interactables.h"
#include "game/mode_dispatch.h"
#include "game/net_types.h"
#include "game/play_area.h"
#include "game/progress_rules.h"
#include "game/rangefinder.h"
#include "game/golf_cart.h"
#include "game/group_round.h"
#include "game/scorecard.h"
#include "game/shot_ability.h"
#include "game/shot_simulation.h"
#include "game/swing.h"
#include "game/text_ids.h"
#include "game/weather.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"
#include "renderer/cart_batch.h"
#include "renderer/remote_avatar_batch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace {
// Where my ball is; the shot rigs and the preview only run while it is out.
glm::vec3 my_ball_position(const game_state& game) {
    return game.ball ? game.ball->position : game.player.position;
}

const club_definition* club_in_hand(const game_state& game) {
    return game.selected_club < game.clubs.size() ? &game.clubs[game.selected_club] : nullptr;
}

// Following a shot: beside where it was hit on its stance's side, looking at the ball.
camera_view follow_view(const game_state& game, const float fov_degrees) {
    const camera_tuning& camera = game.tuning.camera;
    const shot_setup& setup = game.address.setup;
    const glm::vec3 forward = yaw_direction(setup.aim_angle);
    const glm::vec3 side = yaw_left(forward) * hand_sign(setup.hand);
    camera_view view;
    view.position = game.shot_start_position + side * camera.follow_side_distance - forward * camera.follow_back_distance +
        world_up * camera.follow_eye_height;
    view.target = my_ball_position(game) + world_up * camera.follow_look_height;
    view.fov_degrees = fov_degrees;
    return view;
}

// The model of item `id`, empty for no such item.
std::string item_model(const game_state& game, const std::string& id) {
    const item_definition* item = game.items.find(id);
    return item != nullptr ? item->model : std::string();
}

std::optional<render_item_use> shown_use(const game_state& game, const std::optional<item_use_shown>& use) {
    return use ? std::optional<render_item_use>(render_item_use{item_model(game, use->item), use->elapsed}) : std::nullopt;
}

// My bag's model.
std::string my_bag_model(const game_state& game) {
    return item_model(game, equipped_loadout(game).bag);
}

// My figure as others see me, stood to my setup holding the club while I
// address my ball, else carrying it, my bag on my back when it is there.
render_remote_avatar my_figure(const game_state& game, const render_data& data) {
    render_remote_avatar me;
    me.position = game.player.position;
    me.yaw = game.player.yaw;
    me.left_handed = game.hand == handedness::left;
    if (data.my_club) {
        me.swing = data.my_club;
        me.yaw = address_facing_yaw(game.address.setup);
    } else if (const club_definition* club = club_in_hand(game)) {
        me.carried_club_model = club->model;
    }
    if (bag_on_back(game)) {
        me.bag_model = my_bag_model(game);
    }
    return me;
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

// Every hole's pin but the one my ball is in play on, which is drawn as `pin_position`.
std::vector<glm::vec3> other_pin_markers(const game_state& game) {
    std::vector<glm::vec3> pins;
    const std::vector<glm::vec3>& all = game.static_anchors.pins;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!ball_in_play(game) || i != game.ball->hole) {
            pins.push_back(all[i]);
        }
    }
    return pins;
}

// What the course map shows: the course's holes, each numbered along its line of play.
void add_course_map(render_data& data, const game_state& game, const text_assets& text) {
    data.course_map_low = game.area.holes_low;
    data.course_map_high = game.area.holes_high;
    if (!bag_on_back(game)) {
        data.map_bag_position = bag_position(game);
    }
    for (const hole_sign& sign : game.area.signs) {
        if (sign.line_of_play.empty()) {
            continue;
        }
        const std::size_t number = sign.area_hole + 1;
        data.map_holes.push_back(render_map_hole{
            format_text(text, text_course_map_hole_number, {{"hole", std::to_string(number)}}), sign.line_of_play});
    }
}

// The rangefinder's reading, and the wind as it blows across my view.
void add_rangefinder(render_data& data, const game_state& game, const text_assets& text) {
    const rangefinder_reading& reading = game.rangefinder;
    data.rangefinder_distance_label = reading.distance_meters
        ? format_text(text, text_hud_rangefinder, {{"meters", std::to_string(rounded_rangefinder_meters(*reading.distance_meters))}})
        : lookup_text(text, text_hud_rangefinder_none);
    if (reading.height_meters) {
        const int meters = static_cast<int>(std::lround(*reading.height_meters));
        const char* sign = meters > 0 ? text_hud_rangefinder_uphill : text_hud_rangefinder_downhill;
        data.rangefinder_height_label = format_text(text, text_hud_rangefinder_height,
                                                    {{"sign", meters != 0 ? lookup_text(text, sign) : ""},
                                                     {"meters", std::to_string(std::abs(meters))}});
    }
    data.rangefinder_flag = reading.flag;
    data.rangefinder_lock_lit = rangefinder_lock_lit(game);
    const rangefinder_tuning& tuning = game.tuning.rangefinder;
    data.rangefinder_eyepiece = render_eyepiece{rangefinder_magnification(game), tuning.surround_brightness, tuning.surround_saturation};
    const float speed = wind_speed(game.weather);
    char speed_text[16];
    std::snprintf(speed_text, sizeof(speed_text), "%.1f", static_cast<double>(speed));
    data.rangefinder_wind_label = format_text(text, text_hud_rangefinder_wind, {{"speed", speed_text}});
    // Still air is what shows as 0.0.
    if (speed >= 0.05f) {
        data.rangefinder_wind_angle = wrap_angle(wind_heading(game.weather) - view_yaw(game));
    }
}

// Standing over my ball: my club at it, the setup on the ground, the
// outline round what the wheel would change, the OSD's preview and setup
// word, the swing clock and the key hints.
void add_address(render_data& data, const game_state& game, const club_definition& club, const text_assets& text) {
    const address_state& address = game.address;
    const float raise = address.swing_elapsed
        ? swing_club_raise(*address.swing_elapsed, address.setup.backswing, swing_tempo_for(club, game.tuning.swing))
        : 0.0f;
    data.my_club = club_pose_at(game, address.setup, raise, club.model);

    const address_marks marks = my_address_marks(game);
    render_address_marks shown;
    shown.face_heel = marks.face_heel;
    shown.face_toe = marks.face_toe;
    shown.stick_back = marks.stick_back;
    shown.stick_front = marks.stick_front;
    shown.stock_tick = marks.stock_tick;
    shown.lead_foot = marks.lead_foot;
    shown.trail_foot = marks.trail_foot;
    shown.ball = marks.ball;
    data.address_marks = shown;
    if (!address.swing_elapsed) {
        switch (my_pointed_part(game)) {
        case setup_part::face:
            data.setup_outline = {marks.face_heel, marks.face_toe};
            break;
        case setup_part::ball:
            data.setup_outline = {data.ball_position};
            break;
        case setup_part::stance:
            data.setup_outline = {marks.stick_back, marks.stick_front};
            break;
        case setup_part::none:
            break;
        }
        data.show_address_hints = true;
    }

    if (address.preview && !address.swing_elapsed) {
        data.osd_line = address.preview->line;
        data.osd_landing = address.preview->landing;
        data.osd_rest = address.preview->rest;
    }
    const golfer_tuning& golfer = game.tuning.golfer;
    const setup_word word =
        setup_word_for(classify_setup(address.setup, club, lie_at(game.area, address.setup.ball_start), golfer), golfer);
    data.setup_word = word.height && word.curve
        ? format_text(text, text_hud_setup_word_pair,
                      {{"height", lookup_text(text, word.height)}, {"curve", lookup_text(text, word.curve)}})
        : lookup_text(text, word.height ? word.height : word.curve ? word.curve : text_hud_setup_word_stock);
    data.show_swing_clock = true;
    data.swing_backswing = address.setup.backswing;
    data.swing_backswing_max = setup_limits_for(club, game.tuning.setup).backswing_max;
}

void add_collectible_markers(render_data& data, const game_state& game) {
    const static_anchor_cache& anchors = game.static_anchors;
    const std::vector<course_world_collectible>& collectibles = game.world.collectibles;
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
        view.position = first_person_eye(game);
        view.target = view.position + first_person_look(game) * camera.walking_look_distance;
        view.fov_degrees = fov_degrees;
        return view;
    }
    case camera_rig::addressing: {
        const eye_view eye = my_address_eye(game);
        camera_view view;
        view.position = eye.position;
        view.target = eye.position + eye.direction * camera.walking_look_distance;
        view.fov_degrees = fov_degrees;
        return view;
    }
    case camera_rig::following_shot:
        return follow_view(game, fov_degrees);
    }
    return camera_view{};
}

namespace {
// The room online, so the two progress stores are never confused (offline
// says so with the datestamp, core/vcr_frame.h). Without a connection or a
// room, it says the game is reconnecting.
std::string mode_label(const game_state& game, const text_assets& text) {
    if (!is_online(game)) {
        return "";
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
    return physics_center +
        world_up * ((scale.ball_visual_radius_meters - scale.ball_physics_radius_meters) / scale.meters_per_world_unit);
}

render_club_pose club_pose_at(const game_state& game, const shot_setup& setup, const float raise,
                              const std::string& club_model) {
    render_club_pose pose;
    pose.ball_position = drawn_ball_center(setup.ball_start, game.tuning.scale);
    pose.aim_angle = setup.aim_angle;
    pose.face_turn = world_face_yaw(setup) - setup.aim_angle;
    if (const club_definition* club = find_club(game.clubs, setup.club_id)) {
        pose.shaft_lean =
            glm::radians(game.tuning.swing.ball_position_loft_degrees) * (club->spec.stock_ball_position - setup.ball_position);
        pose.putt = club->club_class == "putter";
    }
    pose.raise = raise;
    pose.left_handed = setup.hand == handedness::left;
    pose.club_model = club_model;
    return pose;
}

std::vector<hole_sign_text> hole_sign_texts(const game_state& game) {
    std::vector<hole_sign_text> texts;
    for (const hole_sign& sign : game.area.signs) {
        const int par = sign.area_hole < game.course_holes.size() ? game.course_holes[sign.area_hole].par : 0;
        const int meters = rounded_rangefinder_meters(sign.length * game.tuning.scale.meters_per_world_unit);
        texts.push_back(hole_sign_text{static_cast<int>(sign.area_hole) + 1, par, meters});
    }
    return texts;
}

render_data make_render_data(const game_state& game,
                             const text_assets& text,
                             const std::vector<skill_definition>& skills,
                             const render_meshes& meshes,
                             const prop_catalog& props,
                             const float fov_degrees,
                             const bool interact_prompt) {
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
    data.floodlights = meshes.floodlights;
    data.water = meshes.water;
    data.backdrop = &game.course.backdrop;
    data.trees = &game.static_anchors.trees;
    data.trees_revision = game.static_anchors.revision;
    data.area_center = game.area.center;
    data.area_extent = game.area.extent;

    const world_scale_tuning& scale = game.tuning.scale;
    data.player_position = game.player.position;
    data.ball_visual_radius_meters = scale.ball_visual_radius_meters;
    data.cup_radius_meters = game.tuning.cup.radius_meters * game.tuning.cup.leniency;
    data.pin_visual_height_meters = scale.pin_visual_height_meters;
    data.show_ball = game.ball.has_value();
    data.show_hole = ball_in_play(game);
    if (game.ball) {
        // Sitting in the cup: its centre at the rim, the top showing.
        const bool in_cup = game.ball->place == ball_place::in_cup;
        data.ball_position = in_cup ? game.ball->position : drawn_ball_center(game.ball->position, scale);
        data.tee_position = tee_anchor_position(game, game.ball->hole);
        data.pin_position = pin_anchor_position(game, game.ball->hole);
    }
    data.tee_boxes = &game.area.tee_boxes;
    data.pin_markers = other_pin_markers(game);
    for (std::size_t i = 0; i < game.flags_in.size() && i < game.static_anchors.pins.size(); ++i) {
        if (!game.flags_in[i]) {
            data.flags_out.push_back(game.static_anchors.pins[i]);
        }
    }
    for (const solid& stick : flagstick_solids(game.area.cups, game.flags_in, game.tuning)) {
        if (stick.shape == solid_shape::wall) {
            data.lying_flags.emplace_back(stick.base, stick.end);
        }
    }
    add_collectible_markers(data, game);

    const club_definition* club = club_in_hand(game);
    if (game.mode == game_mode::addressing && game.ball && club != nullptr) {
        add_address(data, game, *club, text);
    }

    data.show_flight_path = game.mode == game_mode::following_shot && !game.flight_path_points.empty();
    data.flight_path_points = &game.flight_path_points;
    data.flight_path_color = game.tuning.flight_path.color;
    data.flight_path_alpha = game.tuning.flight_path.alpha;
    data.flight_path_width = game.tuning.flight_path.line_width;

    for (const cart_in_world& cart : carts_in_world(game)) {
        const auto model = props.find(item_model(game, cart.item));
        if (model == props.end()) {
            continue;
        }
        render_cart drawn = cart_tipped(cart.condition)
            ? cart_on_side(cart.pose.position, cart.pose.yaw, cart.condition == cart_condition::on_left_side, model->second,
                           terrain_height(game.area, cart.pose.position))
            : cart_on_ground(cart.pose.position, cart.pose.yaw, model->second,
                             [&](const glm::vec3& point) { return terrain_height(game.area, point); });
        drawn.driven = cart.driven;
        drawn.steer = cart.steer;
        drawn.model = model->first;
        const auto mount = [](const cart_seat seat) { return seat == cart_seat::driver ? "driver_bag" : "passenger_bag"; };
        if (cart.owner == 0 && game.bag.where == bag_where::on_cart) {
            drawn.bags[mount(game.bag.seat)] = my_bag_model(game);
        }
        if (const std::optional<shown_bag> theirs = cart.owner != 0 ? remote_bag(game, cart.owner) : std::nullopt;
            theirs && theirs->place.where == bag_where::on_cart) {
            drawn.bags[mount(theirs->place.seat)] = theirs->model;
        }
        data.carts.push_back(drawn);
    }
    if (course_started(game)) {
        data.bag_shown = game.bag.where == bag_where::on_cart ? bag_pictogram::cart
            : game.bag.where == bag_where::on_back            ? bag_pictogram::back
                                                              : bag_pictogram::ground;
    }
    if (game.bag.where == bag_where::on_ground) {
        data.ground_bags.push_back(render_ground_bag{my_bag_model(game), game.bag.position, game.bag.yaw});
    }
    data.avatar_eye_height = game.tuning.camera.walking_eye_height;
    if (on_foot(game)) {
        data.my_figure = my_figure(game, data);
    }
    data.smoking = shown_use(game, game.smoking);
    data.drinking = shown_use(game, game.drinking);

    data.mode_label = mode_label(game, text);
    data.show_interact_prompt = interact_prompt && interact_available(game);
    data.show_bag_hints = interact_available(game) && nearest_interactable(game)->kind == interactable_kind::bag;
    if (game.picker) {
        render_club_picker picker{{}, game.picker->highlighted, game.selected_club};
        for (const club_definition& each : game.clubs) {
            picker.labels.push_back(each.label);
        }
        data.club_picker = picker;
        data.show_interact_prompt = false;
    }
    if (game.give_up_prompt && game.ball) {
        data.give_up_label = format_text(text, text_hud_give_up, {{"hole", std::to_string(game.ball->hole + 1)}});
    }
    data.stroke_count = game.ball ? game.ball->strokes : 0;
    if (club != nullptr) {
        data.selected_club_label = club->label;
        data.selected_club_name = lookup_text(text, club->name.c_str());
    }
    data.show_rangefinder = game.rangefinder_active;
    if (data.show_rangefinder) {
        add_rangefinder(data, game, text);
        data.show_interact_prompt = false;
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
    add_online_render_data(data, game, text);
    return data;
}
