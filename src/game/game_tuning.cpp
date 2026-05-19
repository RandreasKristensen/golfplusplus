#include "game/game_tuning.h"

#include "game/asset_resolver.h"
#include "game/club_loader.h"
#include "game/course_loader.h"
#include "game/hole_loader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {
std::string asset_path(const std::string& asset_root, const char* relative) {
    return (std::filesystem::path(asset_root) / relative).string();
}

hole_data fallback_hole() {
    hole_data hole;
    hole.id = "fallback_sandbox";
    hole.name = "Fallback Sandbox";
    hole.par = 3;
    hole.wind_seed = 42U;
    hole.tee_position = glm::vec3(0.0f, 0.0f, 0.0f);
    hole.pin_position = glm::vec3(0.0f, 0.0f, 80.0f);
    hole.spline.width = 18.0f;
    hole.spline.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 40.0f),
        glm::vec3(0.0f, 0.0f, 80.0f)
    };
    return hole;
}

}

std::vector<club_definition> fallback_club_definitions() {
    return {
        club_definition{"putter", "Putter", "P", 0, 0, club_stats{22.0f, 2.0f, 0.97f, 0.02f, 0.25f, 0.35f}},
        club_definition{"sand_wedge", "Sand Wedge", "SW", 0, 1, club_stats{25.0f, 54.0f, 0.86f, 0.18f}},
        club_definition{"pitching_wedge", "Pitching Wedge", "PW", 0, 2, club_stats{30.5f, 46.0f, 0.85f, 0.15f}},
        club_definition{"nine_iron", "9 Iron", "9I", 0, 3, club_stats{33.5f, 40.0f, 0.78f, 0.12f}},
        club_definition{"seven_iron", "7 Iron", "7I", 0, 4, club_stats{38.5f, 34.0f, 0.70f, 0.08f}},
        club_definition{"five_iron", "5 Iron", "5I", 0, 5, club_stats{45.0f, 27.0f, 0.62f, 0.06f}},
        club_definition{"seven_wood", "7 Wood", "7W", 0, 6, club_stats{53.0f, 22.0f, 0.55f, 0.045f}},
        club_definition{"four_wood", "4 Wood", "4W", 0, 7, club_stats{65.0f, 16.0f, 0.48f, 0.035f}},
        club_definition{"driver", "Driver", "D", 0, 8, club_stats{80.0f, 11.0f, 0.42f, 0.025f}}
    };
}

void apply_hole_to_tuning(game_tuning& tuning, const hole_data& hole) {
    tuning.course.id = hole.id;
    tuning.course.name = hole.name;
    tuning.course.par = hole.par;
    tuning.course.tee_position = hole.tee_position;
    tuning.course.pin_position = hole.pin_position;
    tuning.course.cup_radius = tuning.scale.cup_physics_radius_meters;
    tuning.course.extent = estimate_course_extent(hole);
    tuning.course.spline = hole.spline;
    tuning.course.material_zones = hole.material_zones;
    tuning.terrain.control_points = hole.spline.control_points;
    tuning.terrain.width = hole.spline.width;
    tuning.terrain.sample_count = 128;
    tuning.terrain_mesh_data = build_terrain_mesh(tuning.terrain, tuning.course.material_zones, tuning.zone_tuning);
    tuning.wind_seed = hole.wind_seed;
    tuning.ground_y = hole.tee_position.y;
}

bool load_hole_runtime(game_tuning& tuning,
                       const course_definition& course,
                       const std::size_t hole_index,
                       const std::string& asset_root) {
    const std::string path = course_hole_path(asset_root, course, hole_index);
    if (path.empty()) {
        return false;
    }

    const std::optional<hole_data> hole = load_hole_from_file(path);
    if (!hole) {
        return false;
    }

    apply_hole_to_tuning(tuning, *hole);
    return true;
}

game_tuning default_game_tuning() {
    return default_game_tuning(resolve_asset_root(""));
}

game_tuning default_game_tuning(const std::string& asset_root) {
    game_tuning tuning;

    tuning.scale.meters_per_world_unit = 1.0f;
    tuning.scale.ball_physics_radius_meters = 0.021335f;
    tuning.scale.ball_mass_kg = 0.04593f;
    tuning.scale.cup_physics_radius_meters = 0.65f;
    tuning.scale.ball_visual_radius_meters = 0.10f;
    tuning.scale.cup_visual_radius_meters = 0.75f;
    tuning.scale.pin_visual_height_meters = 2.10f;

    tuning.zone_tuning.bunker_depth = 0.55f;
    tuning.zone_tuning.water_depth = 0.35f;

    std::vector<course_definition> courses = load_courses_from_directory(asset_path(asset_root, "courses"));
    if (courses.empty()) {
        courses.push_back(fallback_course_definition());
    }
    if (!load_hole_runtime(tuning, courses.front(), 0, asset_root)) {
        const std::optional<hole_data> loaded_hole = load_hole_from_file(asset_path(asset_root, "holes/test.json"));
        apply_hole_to_tuning(tuning, loaded_hole.value_or(fallback_hole()));
    }

    tuning.clubs = load_clubs_from_directory(asset_path(asset_root, "clubs"));
    if (tuning.clubs.empty()) {
        tuning.clubs = fallback_club_definitions();
    }

    tuning.physics.drag_coeff = 0.004f;
    tuning.physics.magnus_coeff = 0.0004f;
    tuning.physics.spin_decay = 0.6f;
    tuning.physics.water_drag_coeff = 0.22f;
    tuning.physics.water_spin_decay = 2.8f;


    tuning.wind.seed_phase_scale = 0.01f;
    tuning.wind.base_speed = 2.0f;
    tuning.wind.speed_variation = 1.5f;
    tuning.wind.speed_time_scale = 0.2f;
    tuning.wind.angle_variation = 0.7f;
    tuning.wind.angle_time_scale = 0.11f;
    tuning.wind.phase_angle_scale = 1.3f;

    tuning.aim_turn_rate = 1.8f;
    tuning.min_swing_power = 0.15f;
    tuning.launch_side_spin_scale = 4.0f;
    tuning.stop_speed = 0.08f;
    tuning.ground_restitution = 0.35f;
    tuning.ground_friction = 0.08f;
    tuning.water_restitution = 0.05f;
    tuning.water_friction = 0.28f;
    tuning.ground_roll_friction = 1.0f;
    tuning.ground_settle_speed = 0.6f;
    tuning.ball_interact_radius = 3.0f;
    tuning.player_walk_speed = 7.0f;
    tuning.player_turn_rate = 1.8f;
    tuning.player_stand_off_distance = 2.0f;

    return tuning;
}
