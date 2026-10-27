#pragma once

// A shot's dice: every random thing about it comes from one 64-bit seed, by
// splitmix64 on integers and normals as a sum of uniforms (Irwin-Hall),
// only adds and multiplies, so a server in WASM rolls what a client rolls.
// Online the server picks the seed; offline a counter in the round does.
// Pure.

#include "game/shot_setup.h"
#include "game/shot_snapshot.h"

#include <cstdint>
#include <vector>

// A number in [0, 1) for draw `draw` of step `step` of the shot. Steps from
// 0 are the ball's (game/shot_simulation.h), shot_strike_step the swing's.
float dice_uniform(std::uint64_t seed, int step, std::uint64_t draw);

inline constexpr int shot_strike_step = -1;

// Near-normal (mean 0, deviation 1, within +-6): twelve uniforms from
// draw `first` on, less six.
float dice_normal(std::uint64_t seed, int step, std::uint64_t first);

// What a swing does besides what was set up, each channel in its own
// units: the club's speed as a share (+0.01 is 1% faster), the face and
// the path in radians (left as yaw), and the strike in metres from the
// face's middle towards the toe and up. As a spread, each is one
// deviation of that channel.
struct swing_errors {
    float speed_share = 0.0f;
    float face = 0.0f;
    float path = 0.0f;
    float strike_toe = 0.0f;
    float strike_high = 0.0f;
};

// `spread` times the shot's dice for the strike.
swing_errors rolled_errors(std::uint64_t seed, const swing_errors& spread);

// A shot as hit: what was set up, its dice, and what moves that it may
// meet as it stood then.
struct shot_record {
    shot_setup setup;
    std::uint64_t seed = 0;
    std::vector<snapshot_entry> snapshot;
};

// The seed of shot number `count` of a round begun with `round_seed`.
std::uint64_t round_shot_seed(std::uint64_t round_seed, int count);
