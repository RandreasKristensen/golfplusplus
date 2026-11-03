#pragma once

// The sounds game code pushes for app to play, each from where it happens
// (audio_event::position): mine and, online, others' as their game shows
// them. How far each carries is the sound's own (assets/sound/sounds.json).

#include "game/club_definition.h"
#include "game/game_state.h"
#include "game/item_catalog.h"
#include "game/shot_simulation.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

void push_audio_event(game_state& state, audio_event_type type, const glm::vec3& position);

// Where someone standing at `standing` smokes and drinks.
glm::vec3 mouth_position(const game_tuning& tuning, const glm::vec3& standing);
// Someone at `standing` using `item`: its use's sound.
void push_item_use(game_state& state, const item_definition& item, const glm::vec3& standing);

// `club` striking the ball where it was: its hit sound, or its mishit sound
// off the middle of its face.
void push_club_hit(game_state& state, const club_definition& club, const shot_result& result, const glm::vec3& ball_start);

// The sounds of `result`'s events from `next_event` on that are `elapsed`
// into the shot, each where the ball was then (with `correction` blended in
// over the shot, as it is shown). The first event not played yet.
std::size_t push_shot_events(game_state& state, const shot_result& result, const glm::vec3& correction, std::size_t next_event,
                             float elapsed);

// A cart whose motor runs: driven and moving.
struct running_motor {
    std::uint64_t owner = 0;  // 0: mine
    glm::vec3 position{0.0f};
    float pitch = 1.0f;       // its loop's playback rate
};
std::vector<running_motor> running_motors(const game_state& state);
