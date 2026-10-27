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

    // [[x, y], ...] with rising x.
    tuning_curve curve(const char* key) const {
        tuning_curve out;
        const json* points = section != nullptr ? json_array(*section, key) : nullptr;
        bool valid = points != nullptr && !points->empty();
        for (std::size_t i = 0; valid && i < points->size(); ++i) {
            const json& point = (*points)[i];
            valid = point.is_array() && point.size() == 2 && point[0].is_number() && point[1].is_number() &&
                (out.points.empty() || point[0].get<float>() > out.points.back().x);
            if (valid) {
                out.points.emplace_back(point[0].get<float>(), point[1].get<float>());
            }
        }
        if (!valid) {
            missing->push_back(name + "." + key);
        }
        return out;
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
    tuning.scale.pin_visual_height_meters = scale.number("pin_visual_height_meters");
    tuning.scale.flagstick_radius_meters = scale.number("flagstick_radius_meters");

    const section_reader terrain = section("terrain");
    tuning.terrain.min_sections = terrain.integer("min_sections");
    tuning.terrain.ground_cell_size = terrain.number("ground_cell_size");
    tuning.terrain.ground_blend_distance = terrain.number("ground_blend_distance");
    tuning.terrain.zones.bunker_depth = terrain.number("bunker_depth");
    tuning.terrain.zones.bunker_lip_meters = terrain.number("bunker_lip_meters");
    tuning.terrain.zones.water_bank_meters = terrain.number("water_bank_meters");
    tuning.terrain.zones.water_shore_meters = terrain.number("water_shore_meters");
    tuning.terrain.zones.water_min_depth = terrain.number("water_min_depth");
    tuning.terrain.zones.water_max_depth = terrain.number("water_max_depth");
    tuning.terrain.fairway_cell_size = terrain.number("fairway_cell_size");
    tuning.terrain.zone_cell_size = terrain.number("zone_cell_size");
    tuning.terrain.relief.scale = terrain.number("relief_scale");
    tuning.terrain.relief.green_scale = terrain.number("green_relief_scale");
    tuning.terrain.relief.green_blend_distance = terrain.number("green_relief_blend_distance");

    const section_reader physics = section("physics");
    tuning.physics.water_drag_coeff = physics.number("water_drag_coeff");
    tuning.physics.water_spin_decay = physics.number("water_spin_decay");

    const section_reader flight = section("flight");
    tuning.physics.aero.mass = flight.number("mass_kg");
    tuning.physics.aero.lift_max = flight.number("lift_max");
    tuning.physics.aero.lift_half_spin = flight.number("lift_half_spin");
    tuning.physics.aero.lift_crisis = flight.number("lift_crisis");
    tuning.physics.aero.drag_base = flight.number("drag_base");
    tuning.physics.aero.drag_spin = flight.number("drag_spin");
    tuning.physics.aero.drag_crisis = flight.number("drag_crisis");
    tuning.physics.aero.crisis_re_start = flight.number("crisis_re_start");
    tuning.physics.aero.crisis_re_end = flight.number("crisis_re_end");
    tuning.physics.aero.spin_decay = flight.number("spin_decay");

    const section_reader air = section("air");
    tuning.physics.air.sea_level_pressure = air.number("sea_level_pressure_pa");
    tuning.physics.air.standard_temperature = air.number("standard_temperature_k");
    tuning.physics.air.lapse_rate = air.number("lapse_rate_k_per_m");
    tuning.physics.air.gas_constant = air.number("gas_constant");
    tuning.physics.air.viscosity = air.number("viscosity_pa_s");

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

    const section_reader temperature = section("temperature");
    tuning.weather.temperature.coldest_hour = temperature.number("coldest_hour");
    tuning.weather.temperature.warmest_hour = temperature.number("warmest_hour");
    tuning.weather.temperature.cloud_range_damping = temperature.number("cloud_range_damping");
    tuning.weather.temperature.rain_cooling_c = temperature.number("rain_cooling_c");
    tuning.weather.temperature.noise_c = temperature.number("noise_c");
    tuning.weather.temperature.noise_hours = temperature.number("noise_hours");

    const section_reader ball = section("ball");
    tuning.ball.stop_speed = ball.number("stop_speed");
    tuning.ball.water_linger_seconds = ball.number("water_linger_seconds");
    tuning.ball.water_restitution = ball.number("water_restitution");
    tuning.ball.water_friction = ball.number("water_friction");
    tuning.ball.settle_speed = ball.number("settle_speed");

    const section_reader surfaces = section("surfaces");
    const auto surface = [&](const char* name, const bool own_stimp) {
        const section_reader of{surfaces.section != nullptr ? json_object(*surfaces.section, name) : nullptr,
                                std::string("surfaces.") + name, &missing};
        surface_tuning out;
        out.dry.contact_angle_scale = of.number("contact_angle_scale");
        out.dry.restitution_scale = of.number("restitution_scale");
        out.dry.friction = of.number("friction");
        out.dry.stimp_feet = own_stimp ? of.number("stimp_feet") : 0.0f;
        out.dry.grass_drag = of.number("grass_drag");
        out.dry.plug_speed = of.number("plug_speed");
        out.dry.plug_depth = of.number("plug_depth");
        out.wet_stimp_scale = of.number("wet_stimp_scale");
        out.wet_contact_angle_scale = of.number("wet_contact_angle_scale");
        out.wet_restitution_scale = of.number("wet_restitution_scale");
        out.wet_friction_scale = of.number("wet_friction_scale");
        out.wet_plug_speed_scale = of.number("wet_plug_speed_scale");
        return out;
    };
    tuning.surfaces.fairway = surface("fairway", true);
    tuning.surfaces.rough = surface("rough", true);
    tuning.surfaces.green = surface("green", false);
    tuning.surfaces.bunker = surface("bunker", true);
    const section_reader cart_surfaces{surfaces.section != nullptr ? json_object(*surfaces.section, "cart") : nullptr,
                                       "surfaces.cart", &missing};
    const auto cart_surface = [&](const char* name) {
        const section_reader of{cart_surfaces.section != nullptr ? json_object(*cart_surfaces.section, name) : nullptr,
                                std::string("surfaces.cart.") + name, &missing};
        return cart_surface_tuning{of.number("rolling_resistance"), of.number("grip"),
                                   of.number("wet_rolling_resistance_scale"), of.number("wet_grip_scale")};
    };
    tuning.surfaces.cart.path = cart_surface("path");
    tuning.surfaces.cart.fairway = cart_surface("fairway");
    tuning.surfaces.cart.rough = cart_surface("rough");
    tuning.surfaces.cart.green = cart_surface("green");
    tuning.surfaces.cart.bunker = cart_surface("bunker");
    tuning.surfaces.cart.water = cart_surface("water");

    const section_reader cup = section("cup");
    tuning.cup.radius_meters = cup.number("radius_meters");
    tuning.cup.depth_meters = cup.number("depth_meters");
    tuning.cup.leniency = cup.number("leniency");
    tuning.cup.rim_radius_meters = cup.number("rim_radius_meters");
    tuning.cup.contact.rim_restitution = cup.number("rim_restitution");
    tuning.cup.contact.rim_friction = cup.number("rim_friction");
    tuning.cup.contact.liner_restitution = cup.number("liner_restitution");
    tuning.cup.contact.liner_friction = cup.number("liner_friction");
    tuning.cup.flagstick_radius_meters = cup.number("flagstick_radius_meters");
    tuning.cup.wrong_cup_drop_distance_meters = cup.number("wrong_cup_drop_distance_meters");

    const section_reader swing = section("swing");
    tuning.swing.backswing_seconds = swing.number("backswing_seconds");
    tuning.swing.downswing_seconds = swing.number("downswing_seconds");
    tuning.swing.putt_backswing_seconds = swing.number("putt_backswing_seconds");
    tuning.swing.putt_downswing_seconds = swing.number("putt_downswing_seconds");
    tuning.swing.speed_share = swing.curve("speed_share");
    tuning.swing.putting_speed_share = swing.curve("putting_speed_share");
    tuning.swing.ball_position_loft_degrees = swing.number("ball_position_loft_degrees");
    tuning.swing.ball_position_attack_degrees = swing.number("ball_position_attack_degrees");
    tuning.swing.pure_strike_centredness = swing.number("pure_strike_centredness");

    const section_reader setup = section("setup");
    tuning.setup.aim_step_degrees = setup.number("aim_step_degrees");
    tuning.setup.face_step_degrees = setup.number("face_step_degrees");
    tuning.setup.ball_position_step = setup.number("ball_position_step");
    tuning.setup.backswing_steps = setup.integer("backswing_steps");
    tuning.setup.putter_backswing_steps = setup.integer("putter_backswing_steps");
    tuning.setup.overswing_max = setup.number("overswing_max");
    tuning.setup.ball_position_meters = setup.number("ball_position_meters");
    tuning.setup.repeat_delay_seconds = setup.number("repeat_delay_seconds");
    tuning.setup.repeat_rate = setup.number("repeat_rate");
    tuning.setup.repeat_acceleration = setup.number("repeat_acceleration");
    tuning.setup.max_repeat_rate = setup.number("max_repeat_rate");
    tuning.setup.pointer_radius_degrees = setup.number("pointer_radius_degrees");
    tuning.setup.drag_threshold_counts = setup.integer("drag_threshold_counts");
    tuning.setup.face_line_half_length_meters = setup.number("face_line_half_length_meters");
    tuning.setup.stick_out_meters = setup.number("stick_out_meters");
    tuning.setup.stick_half_length_meters = setup.number("stick_half_length_meters");

    const section_reader player = section("player");
    tuning.player.walk_speed = player.number("walk_speed");
    tuning.player.turn_rate = player.number("turn_rate");
    tuning.player.look_pitch_rate = player.number("look_pitch_rate");
    tuning.player.max_look_pitch = player.number("max_look_pitch");
    tuning.player.mouse_look_radians = player.number("mouse_look_radians");
    tuning.player.ball_interact_radius = player.number("ball_interact_radius");
    tuning.player.flag_reach = player.number("flag_reach");
    tuning.player.bag_reach = player.number("bag_reach");
    tuning.player.bag_set_down_distance = player.number("bag_set_down_distance");
    tuning.player.ball_stand_off_distance = player.number("ball_stand_off_distance");
    tuning.player.jump_speed = player.number("jump_speed");
    tuning.player.jump_gravity = player.number("jump_gravity");
    tuning.player.body_radius = player.number("body_radius");
    tuning.player.step_up = player.number("step_up");

    const section_reader cart = section("cart");
    tuning.cart.steering_wheel_rate = cart.number("steering_wheel_rate");
    tuning.cart.time_scale = cart.number("time_scale");
    tuning.cart.seat_offset = cart.number("seat_offset");
    tuning.cart.bag_behind_seat = cart.number("bag_behind_seat");
    tuning.cart.board_reach = cart.number("board_reach");
    tuning.cart.step_out_distance = cart.number("step_out_distance");
    tuning.cart.step_seconds = cart.number("step_seconds");
    tuning.cart.park_ahead_distance = cart.number("park_ahead_distance");
    tuning.cart.park_side_distance = cart.number("park_side_distance");
    tuning.cart.park_spacing = cart.number("park_spacing");
    tuning.cart.road_reach_margin = cart.number("road_reach_margin");
    tuning.cart.min_road_reach = cart.number("min_road_reach");
    tuning.cart.body_half_width = cart.number("body_half_width");
    tuning.cart.body_front = cart.number("body_front");
    tuning.cart.body_back = cart.number("body_back");
    tuning.cart.step_up = cart.number("step_up");
    tuning.cart.crash_sound_speed = cart.number("crash_sound_speed");
    tuning.cart.scrape_friction = cart.number("scrape_friction");
    tuning.cart.motor_idle_pitch = cart.number("motor_idle_pitch");
    tuning.cart.motor_top_speed_pitch = cart.number("motor_top_speed_pitch");
    tuning.cart.motor_load_pitch = cart.number("motor_load_pitch");

    const section_reader camera = section("camera");
    tuning.camera.walking_eye_height = camera.number("walking_eye_height");
    tuning.camera.walking_look_distance = camera.number("walking_look_distance");
    tuning.camera.address_eye_height = camera.number("address_eye_height");
    tuning.camera.address_eye_out = camera.number("address_eye_out");
    tuning.camera.address_target_yaw_degrees = camera.number("address_target_yaw_degrees");
    tuning.camera.address_target_pitch_degrees = camera.number("address_target_pitch_degrees");
    tuning.camera.address_down_yaw_degrees = camera.number("address_down_yaw_degrees");
    tuning.camera.address_down_pitch_degrees = camera.number("address_down_pitch_degrees");
    tuning.camera.address_min_yaw_degrees = camera.number("address_min_yaw_degrees");
    tuning.camera.address_max_yaw_degrees = camera.number("address_max_yaw_degrees");
    tuning.camera.address_min_pitch_degrees = camera.number("address_min_pitch_degrees");
    tuning.camera.address_max_pitch_degrees = camera.number("address_max_pitch_degrees");
    tuning.camera.address_snap_seconds = camera.number("address_snap_seconds");
    tuning.camera.follow_side_distance = camera.number("follow_side_distance");
    tuning.camera.follow_back_distance = camera.number("follow_back_distance");
    tuning.camera.follow_eye_height = camera.number("follow_eye_height");
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

    const section_reader shot_preview = section("shot_preview");
    tuning.shot_preview.step_seconds = shot_preview.number("step_seconds");
    tuning.shot_preview.putt_line_meters = shot_preview.number("putt_line_meters");
    tuning.shot_preview.putt_line_step_meters = shot_preview.number("putt_line_step_meters");

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
    tuning.server.cart_condition_slack = server.number("cart_condition_slack");

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

    const section_reader solids = section("solids");
    const auto contact = [&](const char* kind) {
        const section_reader of_kind{solids.section != nullptr ? json_object(*solids.section, kind) : nullptr,
                                     std::string("solids.") + kind, &missing};
        return solid_contact_tuning{of_kind.number("restitution"), of_kind.number("friction")};
    };
    tuning.solids.trunk = contact("trunk");
    tuning.solids.leaves = contact("leaves");
    tuning.solids.pole = contact("pole");
    tuning.solids.net = contact("net");
    tuning.solids.tee_box = contact("tee_box");
    tuning.solids.board = contact("board");
    tuning.solids.flagstick = contact("flagstick");
    tuning.solids.lamp = contact("lamp");
    tuning.solids.cart = contact("cart");
    tuning.solids.person = contact("person");
    tuning.solids.ball = contact("ball");
    tuning.solids.self_hit_grace_seconds = solids.number("self_hit_grace_seconds");
    tuning.solids.leaves_drag_per_meter = solids.number("leaves_drag_per_meter");
    tuning.solids.leaves_scatter_degrees_per_meter = solids.number("leaves_scatter_degrees_per_meter");

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
    tuning.floodlights.head_width = floodlights.number("head_width");
    tuning.floodlights.head_height = floodlights.number("head_height");
    tuning.floodlights.head_depth = floodlights.number("head_depth");
    tuning.floodlights.obstacle_clearance = floodlights.number("obstacle_clearance");
    tuning.floodlights.nudge_step = floodlights.number("nudge_step");
    tuning.floodlights.nudge_tries = floodlights.integer("nudge_tries");

    const section_reader sky_clock = section("sky_clock");
    tuning.sky_clock.anchor_unix_ms =
        sky_clock.date("anchor_real_date") * static_cast<std::int64_t>(milliseconds_per_day);
    tuning.sky_clock.anchor_game_day = sky_clock.number("anchor_game_hour") / 24.0;
    tuning.sky_clock.game_days_per_real_day = sky_clock.number("game_days_per_real_day");
    tuning.sky_clock.year = sky_clock.integer("year");

    const section_reader golfer = section("golfer");
    tuning.golfer.speed_share_spread = golfer.number("speed_share_spread");
    tuning.golfer.face_spread_degrees = golfer.number("face_spread_degrees");
    tuning.golfer.path_spread_degrees = golfer.number("path_spread_degrees");
    tuning.golfer.strike_toe_spread_meters = golfer.number("strike_toe_spread_meters");
    tuning.golfer.strike_high_spread_meters = golfer.number("strike_high_spread_meters");
    tuning.golfer.spread_at_level_one = golfer.number("spread_at_level_one");
    tuning.golfer.overswing_spread = golfer.number("overswing_spread");
    tuning.golfer.natural_shape_spread = golfer.number("natural_shape_spread");
    tuning.golfer.natural_face_bias_degrees = golfer.number("natural_face_bias_degrees");
    tuning.golfer.curve_full_degrees = golfer.number("curve_full_degrees");
    tuning.golfer.ball_full = golfer.number("ball_full");
    tuning.golfer.flop_start_degrees = golfer.number("flop_start_degrees");
    tuning.golfer.flop_full_degrees = golfer.number("flop_full_degrees");
    tuning.golfer.word_threshold = golfer.number("word_threshold");

    const section_reader lies = section("lies");
    tuning.lies.rough_grip_share = lies.number("rough_grip_share");
    tuning.lies.wet_grip_loss = lies.number("wet_grip_loss");
    tuning.lies.rough_speed_loss = lies.number("rough_speed_loss");
    tuning.lies.rough_face_close_per_loft = lies.number("rough_face_close_per_loft");
    tuning.lies.sand_grip_share = lies.number("sand_grip_share");
    tuning.lies.sand.speed_share = lies.number("explosion_speed_share");
    tuning.lies.sand.spin_share = lies.number("explosion_spin_share");
    tuning.lies.sand.entry_blend_degrees = lies.number("explosion_entry_blend_degrees");
    tuning.lies.sand.descent_blend_degrees = lies.number("explosion_descent_blend_degrees");
    tuning.lies.stance_ball_distance_meters = lies.number("stance_ball_distance_meters");
    tuning.lies.stance_width_meters = lies.number("stance_width_meters");

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
