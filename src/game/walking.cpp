#include "game/walking.h"

#include "game/body_contact.h"
#include "game/effects.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "game/player_jump.h"
#include "game/world_sounds.h"

#include <cstdint>
#include <vector>

namespace {
// Within a hair of a whole step counts as one: frame times summed in floats
// fall just short of it.
constexpr float step_rounding = 1.0e-5f;

float rise_over_ground(const game_state& state, const glm::vec3& feet, frame_profile* profile) {
    return feet.y - terrain_height(state.area, feet, profile);
}
}

void walk_steps(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    player_state& player = state.player;
    const float rise_before = rise_over_ground(state, player.position, profile);
    player.hop_pending = player.hop_pending || input.jump;
    player.unstepped_seconds += dt;
    const std::vector<solid> moving = walking_obstacles(state);
    const float walk_speed = effective_value(state.tuning.player.walk_speed, effect_target_walk_speed, current_effects(state),
                                             shot_unix_ms(state));
    const walk_world world{state.area, moving, state.tuning.player, walk_speed, profile};
    while (player.unstepped_seconds >= move_step_seconds - step_rounding) {
        player.unstepped_seconds -= move_step_seconds;
        move_input step;
        step.kind = move_kind::walk;
        step.forward = static_cast<std::int8_t>(walk_direction(input));
        step.side = static_cast<std::int8_t>(strafe_direction(input));
        step.run = input.run_held;
        step.jump = player.hop_pending;
        step.yaw = quantise_yaw(player.yaw);
        player.hop_pending = false;
        record_step(state, step);
        const walk_step walked = step_walker(
            walker{player.position, player.vertical, player.step_speed, player.step_move_angle, player.against_obstacle}, step,
            world);
        player.position = walked.after.position;
        player.vertical = walked.after.vertical;
        player.step_speed = walked.after.speed;
        player.step_move_angle = walked.after.move_angle;
        player.against_obstacle = walked.after.against_obstacle;
        if (walked.bumped) {
            push_audio_event(state, audio_event_type::player_bump, player.position);
        }
        player.speed = player.step_speed;
        player.move_angle = player.step_move_angle;
        stepped_on_foot(state);
    }
    player.speed = player.step_speed;
    player.move_angle = player.step_move_angle;
    const float rise = rise_over_ground(state, player.position, profile);
    if (player.vertical.speed > 0.0f && rise_before <= airborne_height && rise > airborne_height && !state.ball_correction) {
        send_motion_now(state);
    }
}

void stop_walking(game_state& state) {
    player_state& player = state.player;
    player.unstepped_seconds = 0.0f;
    player.hop_pending = false;
    player.step_speed = 0.0f;
    player.step_move_angle = 0.0f;
}
