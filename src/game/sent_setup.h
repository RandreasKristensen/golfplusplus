#pragma once

// A golfer's setup over their ball as others see it: sent with their motion
// while they address it (avatar_motion), quantised as sent. Presentation
// only; the shot itself is take_shot's setup. Pure.

#include "game/club_definition.h"
#include "game/game_tuning.h"
#include "game/shot_setup.h"

#include <cstdint>

struct sent_setup {
    std::uint16_t aim = 0;          // quantise_yaw
    std::int16_t face_open = 0;     // ten-thousandths of a radian
    std::int8_t ball_position = 0;  // hundredths
    std::uint16_t backswing = 0;    // ten-thousandths
};

inline bool operator==(const sent_setup& a, const sent_setup& b) {
    return a.aim == b.aim && a.face_open == b.face_open && a.ball_position == b.ball_position && a.backswing == b.backswing;
}

inline bool operator!=(const sent_setup& a, const sent_setup& b) {
    return !(a == b);
}

sent_setup sent_setup_of(const shot_setup& setup);

// `setup` (its ball, club and hand) with the sent dials.
shot_setup with_sent_setup(const shot_setup& setup, const sent_setup& sent);

// `sent` inside `club`'s limits, as the server keeps it.
sent_setup clamped_sent_setup(const sent_setup& sent, const club_definition& club, const setup_tuning& tuning);
