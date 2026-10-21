#include "game/tuning_loader.h"

#include "game/json_util.h"
#include "game/sky_clock.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {
// Reads fields of one section and records the ones that are missing.
struct section_reader {
    const json* section = nullptr;
    std::string name;
    std::vector<std::string>* missing = nullptr;

    float number(const char* key) const {
        const std::optional<float> value = section != nullptr ? json_float(*section, key) : std::nullopt;
        if (!value) {
            missing->push_back(name + "." + key);
        }
        return value.value_or(0.0f);
    }

    int integer(const char* key) const {
        const std::optional<int> value = section != nullptr ? json_int(*section, key) : std::nullopt;
        if (!value) {
            missing->push_back(name + "." + key);
        }
        return value.value_or(0);
    }

    // Days since 1970-01-01 of a "YYYY-MM-DD" date.
    std::int64_t date(const char* key) const {
        const std::optional<std::string> text = section != nullptr ? json_string(*section, key) : std::nullopt;
        const std::optional<std::int64_t> days = text ? parse_civil_date(*text) : std::nullopt;
        if (!days) {
            missing->push_back(name + "." + key);
        }
        return days.value_or(0);
    }

    // [[height, scale], ...] with rising heights.
    wind_profile profile(const char* key) const {
        wind_profile out;
        const json* points = section != nullptr ? json_array(*section, key) : nullptr;
        bool valid = points != nullptr && !points->empty();
        for (std::size_t i = 0; valid && i < points->size(); ++i) {
            const json& point = (*points)[i];
            valid = point.is_array() && point.size() == 2 && point[0].is_number() && point[1].is_number();
            if (valid) {
                out.points.push_back(wind_profile_point{point[0].get<float>(), point[1].get<float>()});
                valid = i == 0 || out.points[i].height > out.points[i - 1].height;
            }
        }
        if (!valid) {
            missing->push_back(name + "." + key);
        }
        return out;
    }

    glm::vec3 vec3(const char* key) const {
        const std::optional<glm::vec3> value = section != nullptr ? json_vec3(*section, key) : std::nullopt;
        if (!value) {
            missing->push_back(name + "." + key);
        }
        return value.value_or(glm::vec3(0.0f));
    }
};
}

namespace {
game_tuning_parse_result parse_game_tuning(const json& root) {
    game_tuning_parse_result result;
    std::vector<std::string> missing;
    const auto section = [&](const char* name) {
        return section_reader{json_object(root, name), name, &missing};
    };

    game_tuning tuning;

    const section_reader scale = section("scale");
    tuning.scale.meters_per_world_unit = scale.number("meters_per_world_unit");
    tuning.scale.ball_physics_radius_meters = scale.number("ball_physics_radius_meters");
    tuning.scale.ball_visual_radius_meters = scale.number("ball_visual_radius_meters");
    tuning.scale.cup_radius_meters = scale.number("cup_radius_meters");
    tuning.scale.pin_visual_height_meters = scale.number("pin_visual_height_meters");
    tuning.scale.flagstick_radius_meters = scale.number("flagstick_radius_meters");

    const section_reader terrain = section("terrain");
    tuning.terrain.min_sections = terrain.integer("min_sections");
    tuning.terrain.ground_cell_size = terrain.number("ground_cell_size");
    tuning.terrain.ground_blend_distance = terrain.number("ground_blend_distance");
    tuning.terrain.zones.bunker_depth = terrain.number("bunker_depth");
    tuning.terrain.zones.water_depth_per_metre = terrain.number("water_depth_per_metre");
    tuning.terrain.zones.water_min_depth = terrain.number("water_min_depth");
    tuning.terrain.zones.water_max_depth = terrain.number("water_max_depth");
    tuning.terrain.fairway_cell_size = terrain.number("fairway_cell_size");
    tuning.terrain.zone_cell_size = terrain.number("zone_cell_size");
    tuning.terrain.relief.scale = terrain.number("relief_scale");
    tuning.terrain.relief.green_scale = terrain.number("green_relief_scale");
    tuning.terrain.relief.green_blend_distance = terrain.number("green_relief_blend_distance");
    tuning.terrain.material_overlay_spacing = terrain.number("material_overlay_spacing");

    const section_reader physics = section("physics");
    tuning.physics.drag_coeff = physics.number("drag_coeff");
    tuning.physics.magnus_coeff = physics.number("magnus_coeff");
    tuning.physics.spin_decay = physics.number("spin_decay");
    tuning.physics.water_drag_coeff = physics.number("water_drag_coeff");
    tuning.physics.water_spin_decay = physics.number("water_spin_decay");

    const section_reader wind = section("wind");
    tuning.wind.profile = wind.profile("height_profile");
    tuning.wind.hours = wind.number("hours");
    tuning.wind.calm_speed = wind.number("calm_speed");
    tuning.wind.max_speed = wind.number("max_speed");
    tuning.wind.storm_speed = wind.number("storm_speed");
    tuning.wind.direction_hours = wind.number("direction_hours");
    tuning.wind.direction_swing = wind.number("direction_swing");

    const section_reader weather = section("weather");
    tuning.weather.regime_hours = weather.number("regime_hours");
    tuning.weather.regime_weight = weather.number("regime_weight");
    tuning.weather.spell_hours = weather.number("spell_hours");
    tuning.weather.detail_hours = weather.number("detail_hours");
    tuning.weather.detail_weight = weather.number("detail_weight");
    tuning.weather.cloud_start = weather.number("cloud_start");
    tuning.weather.cloud_full = weather.number("cloud_full");
    tuning.weather.cloud_trend = weather.number("cloud_trend");
    tuning.weather.rain_start = weather.number("rain_start");
    tuning.weather.rain_full = weather.number("rain_full");
    tuning.weather.rain_lag_hours = weather.number("rain_lag_hours");
    tuning.weather.sun_shower_keep = weather.number("sun_shower_keep");
    tuning.weather.sun_shower_cloud = weather.number("sun_shower_cloud");
    tuning.weather.light_rain = weather.number("light_rain");
    tuning.weather.rain = weather.number("rain");
    tuning.weather.heavy_rain = weather.number("heavy_rain");
    tuning.weather.thunder_start = weather.number("thunder_start");
    tuning.weather.thunder_full = weather.number("thunder_full");
    tuning.weather.fog_hours = weather.number("fog_hours");
    tuning.weather.fog_regime_weight = weather.number("fog_regime_weight");
    tuning.weather.fog_start = weather.number("fog_start");
    tuning.weather.fog_full = weather.number("fog_full");
    tuning.weather.fog_rain_clear = weather.number("fog_rain_clear");
    tuning.weather.fog_calm_wind = weather.number("fog_calm_wind");
    tuning.weather.fog_wind_clear = weather.number("fog_wind_clear");
    tuning.weather.light_fog = weather.number("light_fog");
    tuning.weather.fog = weather.number("fog");
    tuning.weather.wetting_per_hour = weather.number("wetting_per_hour");
    tuning.weather.drying_per_hour = weather.number("drying_per_hour");
    tuning.weather.history_hours = weather.number("history_hours");
    tuning.weather.step_hours = weather.number("step_hours");
    tuning.weather.rain_drag_coeff = weather.number("rain_drag_coeff");
    tuning.weather.wet_spin_loss = weather.number("wet_spin_loss");
    tuning.weather.wet_bounce_loss = weather.number("wet_bounce_loss");
    tuning.weather.wet_friction_gain = weather.number("wet_friction_gain");
    tuning.weather.wet_roll_gain = weather.number("wet_roll_gain");
    tuning.weather.wet_green_friction_gain = weather.number("wet_green_friction_gain");
    tuning.weather.wet_green_roll_gain = weather.number("wet_green_roll_gain");

    const section_reader ball = section("ball");
    tuning.ball.stop_speed = ball.number("stop_speed");
    tuning.ball.water_linger_seconds = ball.number("water_linger_seconds");
    tuning.ball.ground_restitution = ball.number("ground_restitution");
    tuning.ball.ground_friction = ball.number("ground_friction");
    tuning.ball.water_restitution = ball.number("water_restitution");
    tuning.ball.water_friction = ball.number("water_friction");
    tuning.ball.bunker_restitution = ball.number("bunker_restitution");
    tuning.ball.bunker_friction = ball.number("bunker_friction");
    tuning.ball.bunker_roll_deceleration = ball.number("bunker_roll_deceleration");
    tuning.ball.tree_restitution = ball.number("tree_restitution");
    tuning.ball.tree_friction = ball.number("tree_friction");
    tuning.ball.green_roll_deceleration = ball.number("green_roll_deceleration");
    tuning.ball.fairway_roll_deceleration = ball.number("fairway_roll_deceleration");
    tuning.ball.rough_roll_deceleration = ball.number("rough_roll_deceleration");
    tuning.ball.settle_speed = ball.number("settle_speed");
    tuning.ball.cup_capture_speed = ball.number("cup_capture_speed");
    tuning.ball.cup_lip_capture_scale = ball.number("cup_lip_capture_scale");
    tuning.ball.cup_dunk_scale = ball.number("cup_dunk_scale");

    const section_reader swing = section("swing");
    tuning.swing.meter_cycle_seconds = swing.number("meter_cycle_seconds");
    tuning.swing.min_power = swing.number("min_power");
    tuning.swing.power_curve_exponent = swing.number("power_curve_exponent");
    tuning.swing.side_spin_scale = swing.number("side_spin_scale");

    const section_reader player = section("player");
    tuning.player.walk_speed = player.number("walk_speed");
    tuning.player.turn_rate = player.number("turn_rate");
    tuning.player.aim_turn_rate = player.number("aim_turn_rate");
    tuning.player.look_pitch_rate = player.number("look_pitch_rate");
    tuning.player.max_look_pitch = player.number("max_look_pitch");
    tuning.player.mouse_look_radians = player.number("mouse_look_radians");
    tuning.player.ball_interact_radius = player.number("ball_interact_radius");
    tuning.player.ball_stand_off_distance = player.number("ball_stand_off_distance");
    tuning.player.address_back_distance = player.number("address_back_distance");
    tuning.player.emote_seconds = player.number("emote_seconds");
    tuning.player.jump_speed = player.number("jump_speed");
    tuning.player.jump_gravity = player.number("jump_gravity");
    tuning.player.body_radius = player.number("body_radius");

    const section_reader cart = section("cart");
    tuning.cart.speed = cart.number("speed");
    tuning.cart.turn_rate = cart.number("turn_rate");
    tuning.cart.damping = cart.number("damping");
    tuning.cart.reverse_speed_scale = cart.number("reverse_speed_scale");
    tuning.cart.full_steer_speed = cart.number("full_steer_speed");
    tuning.cart.steering_wheel_rate = cart.number("steering_wheel_rate");
    tuning.cart.seat_offset = cart.number("seat_offset");
    tuning.cart.board_reach = cart.number("board_reach");
    tuning.cart.step_out_distance = cart.number("step_out_distance");
    tuning.cart.step_seconds = cart.number("step_seconds");
    tuning.cart.park_ahead_distance = cart.number("park_ahead_distance");
    tuning.cart.park_side_distance = cart.number("park_side_distance");
    tuning.cart.park_spacing = cart.number("park_spacing");
    tuning.cart.road_reach_margin = cart.number("road_reach_margin");
    tuning.cart.min_road_reach = cart.number("min_road_reach");
    tuning.cart.road_speed_scale = cart.number("road_speed_scale");
    tuning.cart.road_control_scale = cart.number("road_control_scale");
    tuning.cart.off_road_speed_scale = cart.number("off_road_speed_scale");
    tuning.cart.off_road_control_scale = cart.number("off_road_control_scale");
    tuning.cart.body_radius = cart.number("body_radius");
    tuning.cart.body_ahead = cart.number("body_ahead");
    tuning.cart.body_behind = cart.number("body_behind");
    tuning.cart.crash_bounce = cart.number("crash_bounce");
    tuning.cart.crash_sound_speed = cart.number("crash_sound_speed");
    tuning.cart.scrape_friction = cart.number("scrape_friction");

    const section_reader camera = section("camera");
    tuning.camera.walking_eye_height = camera.number("walking_eye_height");
    tuning.camera.walking_look_distance = camera.number("walking_look_distance");
    tuning.camera.aiming_back_distance = camera.number("aiming_back_distance");
    tuning.camera.aiming_eye_height = camera.number("aiming_eye_height");
    tuning.camera.aiming_look_distance = camera.number("aiming_look_distance");
    tuning.camera.aiming_look_height = camera.number("aiming_look_height");
    tuning.camera.address_side_distance = camera.number("address_side_distance");
    tuning.camera.address_back_distance = camera.number("address_back_distance");
    tuning.camera.address_eye_height = camera.number("address_eye_height");
    tuning.camera.address_look_distance = camera.number("address_look_distance");
    tuning.camera.address_look_height = camera.number("address_look_height");
    tuning.camera.follow_look_height = camera.number("follow_look_height");
    tuning.camera.transition_seconds = camera.number("transition_seconds");
    tuning.camera.transition_jump_distance = camera.number("transition_jump_distance");
    tuning.camera.viewmodel_fov_degrees = camera.number("viewmodel_fov_degrees");

    const section_reader rangefinder = section("rangefinder");
    tuning.rangefinder.magnification = rangefinder.number("magnification");
    tuning.rangefinder.raise_seconds = rangefinder.number("raise_seconds");
    tuning.rangefinder.max_range_meters = rangefinder.number("max_range_meters");
    tuning.rangefinder.march_step_meters = rangefinder.number("march_step_meters");
    tuning.rangefinder.refine_steps = rangefinder.integer("refine_steps");
    tuning.rangefinder.flag_lock_degrees = rangefinder.number("flag_lock_degrees");
    tuning.rangefinder.jolt_seconds = rangefinder.number("jolt_seconds");
    tuning.rangefinder.jolt_flashes = rangefinder.integer("jolt_flashes");
    tuning.rangefinder.surround_brightness = rangefinder.number("surround_brightness");
    tuning.rangefinder.surround_saturation = rangefinder.number("surround_saturation");
    tuning.rangefinder.person_height = rangefinder.number("person_height");
    tuning.rangefinder.cart_height = rangefinder.number("cart_height");
    tuning.rangefinder.ping_seconds = rangefinder.number("ping_seconds");
    tuning.rangefinder.light_rain_error = rangefinder.number("light_rain_error");
    tuning.rangefinder.light_fog_error = rangefinder.number("light_fog_error");
    tuning.rangefinder.blank_share = rangefinder.number("blank_share");
    tuning.rangefinder.nonsense_slope = rangefinder.number("nonsense_slope");

    const section_reader aim_preview = section("aim_preview");
    tuning.aim_preview.step_seconds = aim_preview.number("step_seconds");
    tuning.aim_preview.max_points = aim_preview.integer("max_points");

    const section_reader flight_path = section("flight_path");
    tuning.flight_path.color = flight_path.vec3("color");
    tuning.flight_path.alpha = flight_path.number("alpha");
    tuning.flight_path.min_point_spacing = flight_path.number("min_point_spacing");
    tuning.flight_path.max_points = flight_path.integer("max_points");
    tuning.flight_path.line_width = flight_path.number("line_width");

    const section_reader xp_drops = section("xp_drops");
    tuning.xp_drops.lifetime_seconds = xp_drops.number("lifetime_seconds");
    tuning.xp_drops.min_visible_xp = xp_drops.integer("min_visible_xp");

    const section_reader net = section("net");
    tuning.net.motion_heartbeat_seconds = net.number("motion_heartbeat_seconds");
    tuning.net.motion_min_interval_seconds = net.number("motion_min_interval_seconds");
    tuning.net.motion_correction_distance = net.number("motion_correction_distance");
    tuning.net.motion_correction_yaw = net.number("motion_correction_yaw");
    tuning.net.remote_extrapolation_seconds = net.number("remote_extrapolation_seconds");
    tuning.net.remote_correction_seconds = net.number("remote_correction_seconds");
    tuning.net.shot_correction_distance = net.number("shot_correction_distance");
    tuning.net.shot_correction_seconds = net.number("shot_correction_seconds");
    tuning.net.remote_trail_fade_seconds = net.number("remote_trail_fade_seconds");
    tuning.net.group_join_distance = net.number("group_join_distance");
    tuning.net.notice_seconds = net.number("notice_seconds");

    const section_reader server = section("server");
    tuning.server.room_capacity = server.integer("room_capacity");
    tuning.server.group_capacity = server.integer("group_capacity");
    tuning.server.name_min_length = server.integer("name_min_length");
    tuning.server.name_max_length = server.integer("name_max_length");
    tuning.server.link_code_minutes = server.number("link_code_minutes");
    tuning.server.link_codes_per_hour = server.integer("link_codes_per_hour");
    tuning.server.link_failures_per_hour = server.integer("link_failures_per_hour");
    tuning.server.motion_speed_scale = server.number("motion_speed_scale");
    tuning.server.motion_distance_slack = server.number("motion_distance_slack");
    tuning.server.max_motion_gap_seconds = server.number("max_motion_gap_seconds");
    tuning.server.interact_slack = server.number("interact_slack");
    tuning.server.shot_time_slack_seconds = server.number("shot_time_slack_seconds");
    tuning.server.timing_slack_seconds = server.number("timing_slack_seconds");

    const section_reader hole_sign = section("hole_sign");
    tuning.hole_sign.edge_inset = hole_sign.number("edge_inset");
    tuning.hole_sign.forward_offset = hole_sign.number("forward_offset");
    tuning.hole_sign.look_ahead = hole_sign.number("look_ahead");
    tuning.hole_sign.board_width = hole_sign.number("board_width");
    tuning.hole_sign.board_height = hole_sign.number("board_height");
    tuning.hole_sign.board_thickness = hole_sign.number("board_thickness");
    tuning.hole_sign.board_lift = hole_sign.number("board_lift");
    tuning.hole_sign.post_radius = hole_sign.number("post_radius");

    const section_reader tee_box = section("tee_box");
    tuning.tee_box.width = tee_box.number("width");
    tuning.tee_box.length = tee_box.number("length");
    tuning.tee_box.padding = tee_box.number("padding");
    tuning.tee_box.sample_spacing = tee_box.number("sample_spacing");

    const section_reader fence = section("fence");
    tuning.fence.pole_radius = fence.number("pole_radius");
    tuning.fence.net_restitution = fence.number("net_restitution");
    tuning.fence.net_friction = fence.number("net_friction");

    const section_reader floodlights = section("floodlights");
    tuning.floodlights.spacing = floodlights.number("spacing");
    tuning.floodlights.tee_clearance = floodlights.number("tee_clearance");
    tuning.floodlights.green_clearance = floodlights.number("green_clearance");
    tuning.floodlights.fairway_offset = floodlights.number("fairway_offset");
    tuning.floodlights.tee_offset = floodlights.number("tee_offset");
    tuning.floodlights.green_offset = floodlights.number("green_offset");
    tuning.floodlights.pole_height = floodlights.number("pole_height");
    tuning.floodlights.pole_radius = floodlights.number("pole_radius");
    tuning.floodlights.arm_length = floodlights.number("arm_length");
    tuning.floodlights.obstacle_clearance = floodlights.number("obstacle_clearance");
    tuning.floodlights.nudge_step = floodlights.number("nudge_step");
    tuning.floodlights.nudge_tries = floodlights.integer("nudge_tries");

    const section_reader sky_clock = section("sky_clock");
    tuning.sky_clock.anchor_unix_ms =
        sky_clock.date("anchor_real_date") * static_cast<std::int64_t>(milliseconds_per_day);
    tuning.sky_clock.anchor_game_day = sky_clock.number("anchor_game_hour") / 24.0;
    tuning.sky_clock.game_days_per_real_day = sky_clock.number("game_days_per_real_day");
    tuning.sky_clock.year = sky_clock.integer("year");

    // One entry per lie, named as ball_lie lists them.
    const json* lies = json_object(root, "lies");
    const char* const lie_names[ball_lie_count] = {"tee", "fairway", "rough", "green", "bunker", "water"};
    for (std::size_t i = 0; i < ball_lie_count; ++i) {
        const section_reader lie{lies != nullptr ? json_object(*lies, lie_names[i]) : nullptr,
                                 std::string("lies.") + lie_names[i], &missing};
        tuning.lies[i].power = lie.number("power");
        tuning.lies[i].spin = lie.number("spin");
    }

    if (!missing.empty()) {
        result.error = "game tuning is missing or has malformed fields:";
        for (const std::string& field : missing) {
            result.error += " " + field;
        }
        return result;
    }
    result.tuning = tuning;
    return result;
}
}

game_tuning_parse_result parse_game_tuning_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        game_tuning_parse_result result;
        result.error = "game tuning is not a JSON object";
        return result;
    }
    return parse_game_tuning(*root);
}

game_tuning_parse_result parse_game_tuning_from_texts(const std::vector<std::string>& texts) {
    game_tuning_parse_result result;
    json merged = json::object();
    for (const std::string& text : texts) {
        const std::optional<json> file = parse_json(text);
        if (!file || !file->is_object()) {
            result.error = "a game tuning file is not a JSON object";
            return result;
        }
        for (const auto& [name, section] : file->items()) {
            if (merged.contains(name)) {
                result.error = "game tuning section " + name + " is in two files";
                return result;
            }
            merged[name] = section;
        }
    }
    return parse_game_tuning(merged);
}
