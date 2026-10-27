#pragma once

// Values come from assets/play/tuning/ (see game/tuning_loader.h).

#include <vector>

// A golf ball in the air (physics/flight_model.h): its mass, and its lift
// and drag coefficients from its spin parameter S = r w / v and Reynolds
// number Re, fitted to the TrackMan tour averages
// (tests/fixtures/reference/aerodynamics.json) on condition that thinner
// and warmer air carry a drive further, as measured:
//   crisis = Re's way from crisis_re_start to crisis_re_end, 0..1 (the
//            dimpled ball's drag crisis)
//   CL = lift_max S^3 / (S^3 + lift_half_spin^3) x (1 - lift_crisis crisis)
//   CD = drag_base + drag_spin S^2 - drag_crisis crisis
// and its spin's decay, dw/dt = -spin_decay w v / r.
struct ball_aero {
    float mass = 0.0f;  // kg
    float lift_max = 0.0f;
    float lift_half_spin = 0.0f;
    float lift_crisis = 0.0f;
    float drag_base = 0.0f;
    float drag_spin = 0.0f;
    float drag_crisis = 0.0f;
    float crisis_re_start = 0.0f;
    float crisis_re_end = 0.0f;
    float spin_decay = 0.0f;
};

// The air, from the standard atmosphere (ISO 2533) and the day's
// temperature (physics/flight_model.h air_at).
struct air_tuning {
    float sea_level_pressure = 0.0f;     // Pa
    float standard_temperature = 0.0f;  // K at sea level, for how pressure falls with height
    float lapse_rate = 0.0f;            // K per metre up
    float gas_constant = 0.0f;          // J / (kg K), dry air
    float viscosity = 0.0f;             // Pa s
};

struct physics_tuning {
    ball_aero aero;
    air_tuning air;
    // Under water the ball also loses water_drag_coeff x speed^2 (m/s^2)
    // and water_spin_decay of its spin per second.
    float water_drag_coeff = 0.0f;
    float water_spin_decay = 0.0f;
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
