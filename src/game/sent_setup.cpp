#include "game/sent_setup.h"

#include "game/movement_step.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float face_units = 10000.0f;
constexpr float ball_units = 100.0f;
constexpr float backswing_units = 10000.0f;

template <typename T>
T quantised(const float value, const float units) {
    const float scaled = std::isfinite(value) ? value * units : 0.0f;
    const float low = static_cast<float>(std::numeric_limits<T>::min());
    const float high = static_cast<float>(std::numeric_limits<T>::max());
    return static_cast<T>(std::lround(std::clamp(scaled, low, high)));
}
}

sent_setup sent_setup_of(const shot_setup& setup) {
    sent_setup sent;
    sent.aim = quantise_yaw(setup.aim_angle);
    sent.face_open = quantised<std::int16_t>(setup.face_open, face_units);
    sent.ball_position = quantised<std::int8_t>(setup.ball_position, ball_units);
    sent.backswing = quantised<std::uint16_t>(setup.backswing, backswing_units);
    return sent;
}

shot_setup with_sent_setup(const shot_setup& setup, const sent_setup& sent) {
    shot_setup shown = setup;
    shown.aim_angle = yaw_of(sent.aim);
    shown.face_open = static_cast<float>(sent.face_open) / face_units;
    shown.ball_position = static_cast<float>(sent.ball_position) / ball_units;
    shown.backswing = static_cast<float>(sent.backswing) / backswing_units;
    return shown;
}

sent_setup clamped_sent_setup(const sent_setup& sent, const club_definition& club, const setup_tuning& tuning) {
    return sent_setup_of(clamped_setup(with_sent_setup(shot_setup{}, sent), setup_limits_for(club, tuning)));
}
