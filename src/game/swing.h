#pragma once

// Timing swing: the first press starts the power meter, which rises and falls
// continuously; the second press hits at the current power.

enum class swing_phase {
    idle,
    timing
};

struct swing_state {
    swing_phase phase = swing_phase::idle;
    float elapsed = 0.0f;  // seconds since the meter started
    float power = 0.0f;    // 0..1
};

// Meter power after `elapsed` seconds at speed 1: 0 -> 1 -> 0 every `cycle_seconds`.
float sample_swing_power(float elapsed, float cycle_seconds);

// How far a golfer's club is raised `elapsed` seconds into their swing, the
// meter running at a club's `timing_speed`: my meter, and the club others
// see me raise (remote_address_pose in remote_players.h).
float swing_meter_power(float elapsed, float timing_speed, float cycle_seconds);
