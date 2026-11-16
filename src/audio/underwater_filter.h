#pragma once

// What the mix sounds like with the camera under water: a low-pass filter and
// a level drop over everything heard, glided in and out. SDL-free; the audio
// engine runs it as the mixer's post-mix on the audio thread.

#include <array>
#include <atomic>
#include <cstdint>

// assets/sound/sounds.json "underwater".
struct underwater_sound {
    float cutoff_hz = 0.0f;       // low-pass corner when fully under
    float gain = 1.0f;            // level when fully under (0..1)
    float glide_seconds = 0.0f;   // dry to under or back
};

class underwater_filter {
public:
    // Not thread-safe: call with the audio thread stopped or locked. Until
    // then the filter passes everything through.
    void configure(const underwater_sound& sound, int sample_rate);
    // Safe from any thread.
    void set_target(bool underwater);
    // Audio thread: filters interleaved 16-bit samples in place. Fully dry
    // leaves them untouched.
    void process(std::int16_t* samples, int frames, int channels);

private:
    static constexpr int max_channels = 8;
    struct channel_state {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    std::atomic<bool> underwater_{false};
    underwater_sound sound_;
    int sample_rate_ = 0;
    float wet_ = 0.0f;  // 0 dry .. 1 fully under
    std::array<channel_state, max_channels> state_{};
};
