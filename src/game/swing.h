#pragma once

enum class swing_phase {
    idle,
    timing
};

struct swing_state {
    swing_phase phase = swing_phase::idle;
    float elapsed = 0.0f;
    float power = 0.0f;
};

float sample_swing_power(float elapsed);
