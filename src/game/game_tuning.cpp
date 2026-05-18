#include "game/game_tuning.h"

game_tuning default_game_tuning() {
    game_tuning tuning;

    tuning.course.tee_position = glm::vec3(0.0f, 0.0f, 0.0f);
    tuning.course.pin_position = glm::vec3(0.0f, 0.0f, 80.0f);
    tuning.course.cup_radius = 0.75f;
    tuning.course.extent = 140.0f;

    tuning.clubs = {
        club_stats{10.0f, 3.0f, 0.95f, 0.03f},   // P
        club_stats{18.0f, 46.0f, 0.85f, 0.15f},  // PW
        club_stats{28.0f, 34.0f, 0.70f, 0.08f}   // 7I
    };

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
    tuning.ground_roll_friction = 2.4f;
    tuning.ground_settle_speed = 0.6f;

    return tuning;
}
