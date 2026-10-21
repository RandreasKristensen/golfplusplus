#pragma once

// Values come from assets/tuning/game_tuning.json (see game/tuning_loader.h).

#include <vector>

struct physics_tuning {
    float drag_coeff = 0.0f;        // deceleration = drag_coeff * speed^2
    float magnus_coeff = 0.0f;      // acceleration = magnus_coeff * (spin x velocity)
    float spin_decay = 0.0f;        // fraction of spin lost per second
    float water_drag_coeff = 0.0f;  // added to drag_coeff below the water surface
    float water_spin_decay = 0.0f;  // added to spin_decay below the water surface
};

// How the wind slows towards the ground (physics/wind.h): its share of the
// reference wind at heights over the ground, in rising order.
struct wind_profile_point {
    float height = 0.0f;  // meters over the ground
    float scale = 0.0f;
};

struct wind_profile {
    std::vector<wind_profile_point> points;
};
