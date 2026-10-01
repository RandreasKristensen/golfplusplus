#include "game/tuning_loader.h"

#include "game/json_util.h"

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

    glm::vec3 vec3(const char* key) const {
        const std::optional<glm::vec3> value = section != nullptr ? json_vec3(*section, key) : std::nullopt;
        if (!value) {
            missing->push_back(name + "." + key);
        }
        return value.value_or(glm::vec3(0.0f));
    }
};
}

game_tuning_parse_result parse_game_tuning_from_text(const std::string& text) {
    game_tuning_parse_result result;
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        result.error = "game tuning is not a JSON object";
        return result;
    }

    std::vector<std::string> missing;
    const auto section = [&](const char* name) {
        return section_reader{json_object(*root, name), name, &missing};
    };

    game_tuning tuning;

    const section_reader scale = section("scale");
    tuning.scale.meters_per_world_unit = scale.number("meters_per_world_unit");
    tuning.scale.ball_physics_radius_meters = scale.number("ball_physics_radius_meters");
    tuning.scale.ball_visual_radius_meters = scale.number("ball_visual_radius_meters");
    tuning.scale.cup_radius_meters = scale.number("cup_radius_meters");
    tuning.scale.pin_visual_height_meters = scale.number("pin_visual_height_meters");

    const section_reader terrain = section("terrain");
    tuning.terrain.min_sections = terrain.integer("min_sections");
    tuning.terrain.ground_cell_size = terrain.number("ground_cell_size");
    tuning.terrain.ground_blend_distance = terrain.number("ground_blend_distance");
    tuning.terrain.zones.bunker_depth = terrain.number("bunker_depth");
    tuning.terrain.zones.water_depth = terrain.number("water_depth");
    tuning.terrain.material_overlay_lift = terrain.number("material_overlay_lift");

    const section_reader physics = section("physics");
    tuning.physics.drag_coeff = physics.number("drag_coeff");
    tuning.physics.magnus_coeff = physics.number("magnus_coeff");
    tuning.physics.spin_decay = physics.number("spin_decay");
    tuning.physics.water_drag_coeff = physics.number("water_drag_coeff");
    tuning.physics.water_spin_decay = physics.number("water_spin_decay");

    const section_reader wind = section("wind");
    tuning.wind.seed_phase_scale = wind.number("seed_phase_scale");
    tuning.wind.base_speed = wind.number("base_speed");
    tuning.wind.speed_variation = wind.number("speed_variation");
    tuning.wind.speed_time_scale = wind.number("speed_time_scale");
    tuning.wind.angle_variation = wind.number("angle_variation");
    tuning.wind.angle_time_scale = wind.number("angle_time_scale");
    tuning.wind.phase_angle_scale = wind.number("phase_angle_scale");

    const section_reader ball = section("ball");
    tuning.ball.stop_speed = ball.number("stop_speed");
    tuning.ball.ground_restitution = ball.number("ground_restitution");
    tuning.ball.ground_friction = ball.number("ground_friction");
    tuning.ball.water_restitution = ball.number("water_restitution");
    tuning.ball.water_friction = ball.number("water_friction");
    tuning.ball.tree_restitution = ball.number("tree_restitution");
    tuning.ball.tree_friction = ball.number("tree_friction");
    tuning.ball.roll_deceleration = ball.number("roll_deceleration");
    tuning.ball.settle_speed = ball.number("settle_speed");

    const section_reader swing = section("swing");
    tuning.swing.meter_cycle_seconds = swing.number("meter_cycle_seconds");
    tuning.swing.min_power = swing.number("min_power");
    tuning.swing.side_spin_scale = swing.number("side_spin_scale");

    const section_reader player = section("player");
    tuning.player.walk_speed = player.number("walk_speed");
    tuning.player.turn_rate = player.number("turn_rate");
    tuning.player.aim_turn_rate = player.number("aim_turn_rate");
    tuning.player.ball_interact_radius = player.number("ball_interact_radius");
    tuning.player.ball_stand_off_distance = player.number("ball_stand_off_distance");
    tuning.player.address_back_distance = player.number("address_back_distance");
    tuning.player.emote_seconds = player.number("emote_seconds");

    const section_reader cart = section("cart");
    tuning.cart.speed = cart.number("speed");
    tuning.cart.turn_rate = cart.number("turn_rate");
    tuning.cart.drift_turn_rate = cart.number("drift_turn_rate");
    tuning.cart.normal_damping = cart.number("normal_damping");
    tuning.cart.drift_damping = cart.number("drift_damping");
    tuning.cart.drift_duration = cart.number("drift_duration");
    tuning.cart.drift_speed_boost = cart.number("drift_speed_boost");
    tuning.cart.road_reach_margin = cart.number("road_reach_margin");
    tuning.cart.min_road_reach = cart.number("min_road_reach");
    tuning.cart.road_speed_scale = cart.number("road_speed_scale");
    tuning.cart.road_control_scale = cart.number("road_control_scale");
    tuning.cart.off_road_speed_scale = cart.number("off_road_speed_scale");
    tuning.cart.off_road_control_scale = cart.number("off_road_control_scale");

    const section_reader camera = section("camera");
    tuning.camera.fov_degrees = camera.number("fov_degrees");
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
