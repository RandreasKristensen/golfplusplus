#include "doctest.h"

#include "audio/underwater_filter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
constexpr int rate = 44100;
constexpr int buffer_frames = 1024;
constexpr double pi = 3.14159265358979323846;
const underwater_sound settings{400.0f, 0.5f, 0.2f};

struct configured_filter : underwater_filter {
    configured_filter() { configure(settings, rate); }
};

bool within(const double value, const double expected, const double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

// Runs a stereo sine of `amplitude` through the filter for `seconds`, in
// mixer-sized buffers; returns the input and output of the final buffer.
struct run_result {
    std::vector<std::int16_t> in;
    std::vector<std::int16_t> out;
    std::size_t sample_at = 0;
};

run_result run(underwater_filter& filter, run_result state, const double hz, const double amplitude, const double seconds) {
    const int buffers = static_cast<int>(std::ceil(seconds * rate / buffer_frames));
    for (int b = 0; b < buffers; ++b) {
        state.in.assign(static_cast<std::size_t>(buffer_frames) * 2, 0);
        for (int f = 0; f < buffer_frames; ++f) {
            const auto v = static_cast<std::int16_t>(std::lround(amplitude * std::sin(2.0 * pi * hz * static_cast<double>(state.sample_at++) / rate)));
            state.in[static_cast<std::size_t>(f) * 2] = v;
            state.in[static_cast<std::size_t>(f) * 2 + 1] = v;
        }
        state.out = state.in;
        filter.process(state.out.data(), buffer_frames, 2);
    }
    return state;
}

double rms(const std::vector<std::int16_t>& samples) {
    double sum = 0.0;
    for (const std::int16_t s : samples) {
        sum += static_cast<double>(s) * s;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

// Output level over input level of the final buffer, after `seconds`.
double settled_ratio(underwater_filter& filter, const double hz, const double seconds) {
    const run_result result = run(filter, {}, hz, 10000.0, seconds);
    return rms(result.out) / rms(result.in);
}
}

TEST_CASE("a dry filter leaves every sample bit-exact") {
    configured_filter filter;
    for (const double hz : {100.0, 5000.0}) {
        const run_result result = run(filter, {}, hz, 30000.0, 0.5);
        CHECK(result.out == result.in);
    }
}

TEST_CASE("an unconfigured filter passes everything through") {
    underwater_filter filter;
    filter.set_target(true);
    const run_result result = run(filter, {}, 5000.0, 20000.0, 0.5);
    CHECK(result.out == result.in);
}

TEST_CASE("under water highs are far quieter than lows, and lows land at the configured level") {
    configured_filter low;
    low.set_target(true);
    const double low_ratio = settled_ratio(low, 100.0, 1.0);
    configured_filter high;
    high.set_target(true);
    const double high_ratio = settled_ratio(high, 5000.0, 1.0);

    CHECK(within(low_ratio, settings.gain, 0.01));
    CHECK(high_ratio < low_ratio * 0.1);  // at least 20 dB down
    CHECK(high_ratio < 0.05);
}

TEST_CASE("the filter takes about its glide time to settle") {
    configured_filter filter;
    (void)run(filter, {}, 100.0, 10000.0, 0.1);
    filter.set_target(true);

    // Halfway through the glide the level is part-way, not yet or already there.
    run_result state = run(filter, {}, 100.0, 10000.0, 0.0);
    state = run(filter, state, 100.0, 10000.0, settings.glide_seconds * 0.5);
    const double halfway = rms(state.out) / rms(state.in);
    CHECK(halfway < 0.9);
    CHECK(halfway > settings.gain + 0.1);

    state = run(filter, state, 100.0, 10000.0, settings.glide_seconds * 2.0);
    CHECK(within(rms(state.out) / rms(state.in), settings.gain, 0.01));
}

TEST_CASE("coming back up ends bit-exact again") {
    configured_filter filter;
    filter.set_target(true);
    run_result state = run(filter, {}, 440.0, 20000.0, 0.5);
    CHECK(state.out != state.in);

    filter.set_target(false);
    state = run(filter, state, 440.0, 20000.0, settings.glide_seconds + 0.1);
    CHECK(state.out == state.in);
}

TEST_CASE("the filter saturates instead of wrapping when it rings past full scale") {
    underwater_filter filter;
    filter.configure(underwater_sound{400.0f, 1.0f, 0.2f}, rate);
    filter.set_target(true);
    // Full-scale square wave, long plateaus: a low-pass overshoots each edge.
    constexpr int half_period = 2000;
    std::int16_t highest = 0;
    for (int frame = 0; frame < rate; frame += buffer_frames) {
        std::vector<std::int16_t> samples(static_cast<std::size_t>(buffer_frames) * 2);
        for (int f = 0; f < buffer_frames; ++f) {
            const std::int16_t v = ((frame + f) / half_period) % 2 == 0 ? INT16_MAX : INT16_MIN;
            samples[static_cast<std::size_t>(f) * 2] = v;
            samples[static_cast<std::size_t>(f) * 2 + 1] = v;
        }
        filter.process(samples.data(), buffer_frames, 2);
        for (int f = 0; f < buffer_frames; ++f) {
            const std::int16_t out = samples[static_cast<std::size_t>(f) * 2];
            const bool high_plateau = ((frame + f) / half_period) % 2 == 0 && (frame + f) % half_period > 1500;
            if (high_plateau && frame > rate / 2) {
                CHECK(out > 30000);  // a wrapped overshoot would be near -32768
                highest = std::max(highest, out);
            }
        }
    }
    CHECK(highest == INT16_MAX);
}
