#pragma once

// Values come from assets/tuning/game_tuning.json (see game/tuning_loader.h).

struct physics_tuning {
    float drag_coeff = 0.0f;        // deceleration = drag_coeff * speed^2
    float magnus_coeff = 0.0f;      // acceleration = magnus_coeff * (spin x velocity)
    float spin_decay = 0.0f;        // fraction of spin lost per second
    float water_drag_coeff = 0.0f;  // added to drag_coeff below the water surface
    float water_spin_decay = 0.0f;  // added to spin_decay below the water surface
};

// Wind speed and direction drift slowly with time; see wind.cpp.
struct wind_tuning {
    float seed_phase_scale = 0.0f;
    float base_speed = 0.0f;
    float speed_variation = 0.0f;
    float speed_time_scale = 0.0f;
    float angle_variation = 0.0f;
    float angle_time_scale = 0.0f;
    float phase_angle_scale = 0.0f;
};
