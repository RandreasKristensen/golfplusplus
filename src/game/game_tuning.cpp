#include "game/game_tuning.h"

#include "game/club_loader.h"

#include <string>
#include <vector>

namespace {
std::string asset_path(const char* relative) {
#ifdef VCR_GOLF_ASSETS_DIR
    std::string base = VCR_GOLF_ASSETS_DIR;
#else
    std::string base = "assets";
#endif
    if (!base.empty() && base.back() != '/' && base.back() != '\\') {
        base.push_back('/');
    }
    return base + relative;
}

std::vector<club_definition> fallback_clubs() {
    return {
        club_definition{"putter", "Putter", "P", 0, 0, club_stats{10.0f, 3.0f, 0.95f, 0.03f}},
        club_definition{"pitching_wedge", "Pitching Wedge", "PW", 0, 1, club_stats{18.0f, 46.0f, 0.85f, 0.15f}},
        club_definition{"seven_iron", "7 Iron", "7I", 0, 2, club_stats{28.0f, 34.0f, 0.70f, 0.08f}}
    };
}
}

game_tuning default_game_tuning() {
    game_tuning tuning;

    tuning.course.tee_position = glm::vec3(0.0f, 0.0f, 0.0f);
    tuning.course.pin_position = glm::vec3(0.0f, 0.0f, 80.0f);
    tuning.course.cup_radius = 0.75f;
    tuning.course.extent = 140.0f;

    tuning.clubs = load_clubs_from_directory(asset_path("clubs"));
    if (tuning.clubs.empty()) {
        tuning.clubs = fallback_clubs();
    }

    tuning.physics.drag_coeff = 0.02f;
    tuning.physics.magnus_coeff = 0.0004f;
    tuning.physics.spin_decay = 0.6f;

    tuning.wind.seed_phase_scale = 0.01f;
    tuning.wind.base_speed = 2.0f;
    tuning.wind.speed_variation = 1.5f;
    tuning.wind.speed_time_scale = 0.2f;
    tuning.wind.angle_variation = 0.7f;
    tuning.wind.angle_time_scale = 0.11f;
    tuning.wind.phase_angle_scale = 1.3f;

    tuning.wind_seed = 42U;
    tuning.aim_turn_rate = 1.8f;
    tuning.min_swing_power = 0.15f;
    tuning.launch_side_spin_scale = 4.0f;
    tuning.stop_speed = 0.08f;
    tuning.ground_y = 0.0f;
    tuning.ground_restitution = 0.35f;
    tuning.ground_friction = 0.08f;
    tuning.ground_roll_friction = 1.0f;
    tuning.ground_settle_speed = 0.6f;
    tuning.ball_interact_radius = 3.0f;
    tuning.player_walk_speed = 7.0f;
    tuning.player_turn_rate = 1.8f;
    tuning.player_stand_off_distance = 2.0f;

    return tuning;
}
