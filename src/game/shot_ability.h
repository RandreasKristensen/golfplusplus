#pragma once

// How well a golfer plays a setup: what shape it is (the setup word reads
// the same classification), whose skills that takes, and from those how
// widely the swing strays (shot_dice.h swing_errors) and how far the dials
// go. Everything is designed as if every skill is 99; the skill plan fills
// the levels. Pure.

#include "game/club_definition.h"
#include "game/game_tuning.h"
#include "game/golfer.h"
#include "game/shot_dice.h"
#include "game/shot_setup.h"
#include "physics/launch.h"

// How much of each shape a setup is, 0..1 each, in the golfer's own hand:
// a face open to the path fades, closed draws; the ball back of the club's
// stock punches, forward goes high; a wedge's face opened wide flops; from
// sand it is a bunker shot, with a putter a putt (and nothing else).
struct shape_weights {
    float fade = 0.0f;
    float draw = 0.0f;
    float punch = 0.0f;
    float high = 0.0f;
    float flop = 0.0f;
    float bunker = 0.0f;
    float putt = 0.0f;
};

shape_weights classify_setup(const shot_setup& setup, const club_definition& club, ball_lie lie,
                             const golfer_tuning& tuning);

// The words naming a setup's shape (text ids, game/text_ids.h): its
// height (punch or high) and its curve (fade, draw or flop), each only
// past tuning's word_threshold; neither is the stock shot.
struct setup_word {
    const char* height = nullptr;
    const char* curve = nullptr;
};

setup_word setup_word_for(const shape_weights& shape, const golfer_tuning& tuning);

struct shot_ability {
    swing_errors spread;  // one deviation of each channel
    setup_limits limits;
};

// `golfer` playing `setup` with `club` from `lie`: the club's skill and
// the shapes' skills blended by the setup's shape weights, wider below
// the top level, narrower playing the golfer's natural shape, wider
// still in overswing.
shot_ability ability_for(const shot_setup& setup, const club_definition& club, ball_lie lie, const golfer_profile& golfer,
                         const game_tuning& tuning);

// How far a golfer's stock face leans to their natural shape (radians,
// open positive): none for a putter.
float natural_face_bias(const club_definition& club, natural_shape natural, const golfer_tuning& tuning);
