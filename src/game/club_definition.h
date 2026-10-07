#pragma once

#include <string>

// Club stats from assets/clubs/*.json.
struct club_stats {
    float power = 0.0f;                // launch speed at full swing power, m/s
    float loft_degrees = 0.0f;         // launch angle above the aim direction
    float backspin = 0.0f;             // backspin per m/s of launch speed (Magnus lift)
    float side_spin = 0.0f;            // spin about the aim axis, curves the shot
    float timing_speed = 1.0f;         // swing meter speed multiplier
    float roll_friction_scale = 1.0f;  // multiplies ground friction while rolling on the green
    float bunker_power = 0.0f;         // share of `power` a shot from a bunker keeps
};

struct club_definition {
    std::string id;
    std::string name;
    std::string label;      // short HUD label, e.g. "7I"
    std::string hit_sound;  // sound id in assets/audio/sounds.json
    int bag_order = 0;      // place in the bag, shortest club first
    club_stats stats;
};
