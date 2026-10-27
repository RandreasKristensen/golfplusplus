#pragma once

// A shot as the player sets it up over the ball, before the swing: where
// the stance aims, the face, where the ball sits in the stance and how far
// the club goes back. Plain data, replayable: the same setup on the same
// course is the same shot. Its limits and stock come from the club; the
// player's hand is applied here and in address_stance_position, so the
// physics only sees world angles. Pure.

#include "game/club_definition.h"
#include "game/game_tuning.h"

#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

enum class handedness : std::uint8_t {
    right,
    left
};

// +1 for a right-hander, -1 for a left-hander: the side a stance and an
// open face turn to, mirrored.
float hand_sign(handedness hand);

struct shot_setup {
    glm::vec3 ball_start{0.0f};  // filled by the game or the server
    std::string club_id;         // the club in hand
    float aim_angle = 0.0f;      // the stance line, which is the swing's path (world yaw)
    float face_open = 0.0f;      // radians from square to the path, + open, - closed, in the player's own hand
    float ball_position = 0.0f;  // -1 back in the stance .. +1 forward
    float backswing = 0.0f;      // 0 .. 1 full .. setup_limits::backswing_max (overswing)
    handedness hand = handedness::right;
    // UTC milliseconds when it was hit, which pick the weather it flies in
    // (shot_course::weather); online the server's clock as I estimate it.
    std::int64_t shot_at = 0;
};

// How far each dial goes with this club.
struct setup_limits {
    float face_open_min = 0.0f;  // radians, the most closed (negative)
    float face_open_max = 0.0f;  // radians, the most open
    float ball_position_min = 0.0f;
    float ball_position_max = 0.0f;
    float backswing_step = 0.0f;  // one step of the dial; also the shortest backswing
    float backswing_max = 0.0f;   // past 1: overswing
};

setup_limits setup_limits_for(const club_definition& club, const setup_tuning& tuning);

// The club's stock setup at `ball` along `aim`: face square (from sand,
// opened its sand_face_open_degrees) and turned `face_lean` more (the
// golfer's natural shape, shot_ability.h natural_face_bias), the ball at
// its stock position, its stock backswing.
shot_setup stock_setup(const club_definition& club, ball_lie lie, const glm::vec3& ball, float aim, handedness hand,
                       float face_lean, const setup_tuning& tuning);

// `setup` with every dial inside `limits`.
shot_setup clamped_setup(const shot_setup& setup, const setup_limits& limits);

// Steps of each dial asked for at once: positive turns the aim left (as
// yaw), opens the face, moves the ball forward and lengthens the backswing.
struct setup_steps {
    int aim = 0;
    int face = 0;
    int ball = 0;
    int backswing = 0;
    // A fine turn of the stance, radians, positive left (a right-drag).
    float aim_radians = 0.0f;
};

shot_setup stepped_setup(const shot_setup& setup, const setup_steps& steps, const setup_limits& limits,
                         const setup_tuning& tuning);

// Where the face points (world yaw): opened to the right of the path for a
// right-hander, to the left for a left-hander.
float world_face_yaw(const shot_setup& setup);

// The aim from `ball` that points the path at `target`, keeping the face as
// it is to the path.
shot_setup aimed_at(const shot_setup& setup, const glm::vec3& target);

// A dial key held for `held_before` seconds last frame and `held_after`
// now (negative: not held): the steps it makes in between. The press
// makes one; after repeat_delay_seconds it repeats at repeat_rate, rising
// by repeat_acceleration per second to max_repeat_rate.
int dial_steps(float held_before, float held_after, const setup_tuning& tuning);
