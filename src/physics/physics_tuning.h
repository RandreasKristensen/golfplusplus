#pragma once

struct physics_tuning {
    float drag_coeff = 0.0f;
    float magnus_coeff = 0.0f;
    float spin_decay = 0.0f;
    float water_drag_coeff = 0.0f;
    float water_spin_decay = 0.0f;
};

struct wind_tuning {
    float seed_phase_scale = 0.0f;
    float base_speed = 0.0f;
    float speed_variation = 0.0f;
    float speed_time_scale = 0.0f;
    float angle_variation = 0.0f;
    float angle_time_scale = 0.0f;
    float phase_angle_scale = 0.0f;
};
