#pragma once

// Reading a green, crouched on foot (C held): my eyes drop low behind the
// ball, and the green round me within green_read.radius_meters shows its
// slope as a lattice of dots every spacing_meters, each sliding downhill
// faster the steeper it is and still where it is level. It shows the
// ground, never the putt: no line, break or pace. Crouched I stand still.
// Presentation only: it never changes a shot.

#include "game/game_input.h"
#include "game/game_tuning.h"
#include "game/play_area.h"

#include <vector>

#include <glm/vec3.hpp>

struct game_state;

struct green_read_dot {
    glm::vec3 position{0.0f};
    float alpha = 0.0f;
};

// Crouching this frame: held, on foot and walking.
bool crouch_held(const game_state& state, const game_input& input);

// Whether I am down reading the green, or going down: what others see.
bool reading_green(const game_state& state);

// Crouches or stands, over crouch_seconds, and runs the dots' clock while crouched.
void update_crouch(game_state& state, const game_input& input, float dt);

// My eye's height over my feet, world units: walking_eye_height standing,
// green_read.eye_height crouched.
float my_eye_height(const game_state& state);

// The green's dots round `center` at `seconds` on the dots' clock: on the
// lattice of the world (it never swims as I move), on green only, none
// within clear_meters of any of `keep_clear` (the cup, the ball), fading
// towards the radius's edge. A dot on a slope of s% slides drift_meters
// downhill in drift_meters / (s * drift_speed_per_percent) seconds, fading
// in and out, then starts again; under level_percent it stays put. Pure.
std::vector<green_read_dot> green_read_dots(const play_area& area,
                                            const glm::vec3& center,
                                            float seconds,
                                            const std::vector<glm::vec3>& keep_clear,
                                            const green_read_tuning& tuning,
                                            const world_scale_tuning& scale);
