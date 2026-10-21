#include "core/lightning_frame.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
// A strike flickers in up to this many pulses, each weaker than the first.
constexpr int max_pulses = 4;
// No strike sooner than this after the last, however thundery.
constexpr float min_gap_seconds = 0.6f;

// The next value in [0, 1) of `state` (xorshift32).
float next_random(std::uint32_t& state) {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return static_cast<float>(state >> 8U) / 16777216.0f;
}

// A fixed value in [0, 1) for pulse `i` of the strike seeded `seed`.
float pulse_value(const float seed, const int i) {
    const float x = std::sin(seed * 127.1f + static_cast<float>(i) * 311.7f) * 43758.5453f;
    return x - std::floor(x);
}

// Seconds to wait for the next strike at `thunder`: as often as
// strikes_per_minute at full thunder, at random.
float strike_wait(std::uint32_t& random, const float thunder, const lightning_style& style) {
    const float per_second = style.strikes_per_minute / 60.0f * clamp01(thunder);
    if (per_second <= 0.0f) {
        return 1.0f;
    }
    return min_gap_seconds - std::log(1.0f - next_random(random)) / per_second;
}
}

lightning_presentation update_lightning(lightning_presentation state, const float thunder, const bool allowed,
                                        const lightning_style& style, const float dt) {
    state.close_claps = 0;
    state.far_claps = 0;
    for (thunder_clap& clap : state.coming) {
        clap.delay -= dt;
        if (clap.delay <= 0.0f) {
            ++(clap.close ? state.close_claps : state.far_claps);
        }
    }
    state.coming.erase(std::remove_if(state.coming.begin(), state.coming.end(),
                                      [](const thunder_clap& clap) { return clap.delay <= 0.0f; }),
                       state.coming.end());

    if (state.strike) {
        state.strike->age += dt;
        if (state.strike->age >= style.flash_seconds) {
            state.strike.reset();
        }
    }
    if (thunder <= 0.0f) {
        state.until_next = strike_wait(state.random, 1.0f, style);
        return state;
    }
    state.until_next -= dt;
    if (state.until_next > 0.0f) {
        return state;
    }
    state.until_next = strike_wait(state.random, thunder, style);
    if (!allowed) {
        return state;
    }
    lightning_strike strike;
    const float around = next_random(state.random) * glm::two_pi<float>();
    const float up = glm::radians(style.top_min_deg + (style.top_max_deg - style.top_min_deg) * next_random(state.random));
    strike.direction = glm::vec3(std::sin(around) * std::cos(up), std::sin(up), std::cos(around) * std::cos(up));
    strike.seed = next_random(state.random) * 100.0f;
    strike.bolt = next_random(state.random) < style.bolt_share;
    state.strike = strike;
    const float meters = style.near_meters + (style.far_meters - style.near_meters) * next_random(state.random);
    state.coming.push_back(thunder_clap{meters / style.sound_speed, meters <= style.close_meters});
    return state;
}

float lightning_flash(const float age, const float seed, const lightning_style& style) {
    if (age < 0.0f || age >= style.flash_seconds || style.pulse_seconds <= 0.0f) {
        return 0.0f;
    }
    const int pulses = 1 + static_cast<int>(pulse_value(seed, 0) * static_cast<float>(max_pulses));
    float flash = 0.0f;
    for (int i = 0; i < pulses; ++i) {
        // The first pulse at once, the rest spread over the flash.
        const float at = i == 0 ? 0.0f : pulse_value(seed, i) * (style.flash_seconds - style.pulse_seconds);
        const float strength = i == 0 ? 1.0f : 0.4f + 0.5f * pulse_value(seed, i + max_pulses);
        if (age >= at) {
            flash = std::max(flash, strength * std::max(0.0f, 1.0f - (age - at) / style.pulse_seconds));
        }
    }
    return flash;
}

render_lightning make_render_lightning(const lightning_presentation& state, const lightning_style& style) {
    render_lightning out;
    out.color = style.color;
    out.bolt_width = glm::radians(style.bolt_width_deg);
    out.bolt_jitter = glm::radians(style.bolt_jitter_deg);
    out.bolt_brightness = style.bolt_brightness;
    out.glow_radius = glm::radians(style.glow_radius_deg);
    out.glow_strength = style.glow_strength;
    if (state.strike) {
        out.direction = state.strike->direction;
        out.seed = state.strike->seed;
        out.bolt = state.strike->bolt;
        out.flash = lightning_flash(state.strike->age, state.strike->seed, style);
    }
    return out;
}
