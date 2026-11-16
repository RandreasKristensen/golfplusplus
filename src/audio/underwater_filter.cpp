#include "audio/underwater_filter.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double filter_q = 0.70710678;  // Butterworth: flat to the corner
constexpr float dry_cutoff_hz = 20000.0f;
constexpr double pi = 3.14159265358979323846;

struct biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// RBJ cookbook low-pass.
biquad low_pass(const double cutoff_hz, const int sample_rate) {
    const double w0 = 2.0 * pi * cutoff_hz / sample_rate;
    const double alpha = std::sin(w0) / (2.0 * filter_q);
    const double cosine = std::cos(w0);
    const double a0 = 1.0 + alpha;
    biquad f;
    f.b0 = (1.0 - cosine) * 0.5 / a0;
    f.b1 = (1.0 - cosine) / a0;
    f.b2 = f.b0;
    f.a1 = -2.0 * cosine / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

std::int16_t saturate(const double sample) {
    return static_cast<std::int16_t>(std::lround(std::clamp(sample, -32768.0, 32767.0)));
}
}

void underwater_filter::configure(const underwater_sound& sound, const int sample_rate) {
    sound_ = sound;
    sample_rate_ = sample_rate;
    wet_ = underwater_.load() ? 1.0f : 0.0f;
    state_ = {};
}

void underwater_filter::set_target(const bool underwater) {
    underwater_.store(underwater);
}

// The filter always runs so its state follows the signal and wet-in has no
// click; it is only heard once wet_ rises above zero. The corner is set once a
// buffer, the level every sample.
void underwater_filter::process(std::int16_t* const samples, const int frames, const int channels) {
    if (sample_rate_ <= 0 || sound_.glide_seconds <= 0.0f || channels < 1 || channels > max_channels) {
        return;
    }
    const float target = underwater_.load() ? 1.0f : 0.0f;
    const float step = 1.0f / (sound_.glide_seconds * static_cast<float>(sample_rate_));
    const double dry = std::min(static_cast<double>(dry_cutoff_hz), 0.45 * sample_rate_);
    const double cutoff = std::min(dry, static_cast<double>(sound_.cutoff_hz));
    const biquad f = low_pass(dry * std::pow(cutoff / dry, static_cast<double>(wet_)), sample_rate_);

    for (int frame = 0; frame < frames; ++frame) {
        wet_ = wet_ < target ? std::min(target, wet_ + step) : std::max(target, wet_ - step);
        const double gain = 1.0 + static_cast<double>(wet_) * (static_cast<double>(sound_.gain) - 1.0);
        for (int c = 0; c < channels; ++c) {
            std::int16_t& sample = samples[static_cast<std::size_t>(frame) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
            channel_state& s = state_[static_cast<std::size_t>(c)];
            const double x = sample;
            const double y = f.b0 * x + s.z1;
            s.z1 = f.b1 * x - f.a1 * y + s.z2;
            s.z2 = f.b2 * x - f.a2 * y;
            if (wet_ > 0.0f) {
                sample = saturate(y * gain);
            }
        }
    }
}
