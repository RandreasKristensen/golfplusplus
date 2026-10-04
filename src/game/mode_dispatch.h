#pragma once

// Where offline and online play part ways. Gameplay reports what the player
// did through these, and nothing else in game code writes progress or pushes
// net commands. Offline applies progress_rules to the local save at once;
// online pushes a net_command, and the result comes back from the server
// through receive_online_progress.

#include "game/course_world_definition.h"
#include "game/game_state.h"
#include "game/net_types.h"
#include "game/reward_rules.h"
#include "game/save_data.h"
#include "game/shot_simulation.h"

#include <cstddef>

bool is_online(const game_state& state);

// The progress the player sees and plays with: the local save offline, the
// server's online. Everything that shows or checks progress reads this.
const save_data& active_progress(const game_state& state);

// A shot was hit as stroke state.stroke_count. Offline: swing XP.
// Online: take_shot.
void record_shot(game_state& state, const shot_input& input);

// Offline: the smoke XP (drinking earns none). Online: the emote.
void record_emote(game_state& state, emote_id emote);

// The player walked or drove `meters`. Offline: movement XP at `rate`, the
// leftover kept in `pending_meters`. Online the server counts movement from
// motion updates, so nothing happens here.
void record_movement(game_state& state, const movement_xp_rate& rate, float& pending_meters, float meters);

// Offline: claims it on the save. Online: claim_collectible.
void record_collectible_claim(game_state& state, const course_world_collectible& collectible);

// The ball went back to the tee. Online: retee, so the server's ball does
// too. Offline needs nothing.
void record_retee(game_state& state);

// Every hole entered, hub or not. Online: enter_hole. Offline needs nothing.
void record_hole_started(game_state& state, std::size_t hole_index);

// Online: the server still has the player on a hole this client has left:
// return_to_hub gives it up there too (no score). Offline needs nothing.
void record_hole_given_up(game_state& state);

// G: join the group of the nearest player (net.group_join_distance, same
// zone) when it has space, else start a group. Shift+G leaves. Online only.
void request_group(game_state& state);
void request_leave_group(game_state& state);

// A shot's wind time: offline the hole's own clock (hole_time); online the
// server's clock since it started the hole, which it checks the shot by.
float shot_wind_time(const game_state& state);

// Whether a cigarette is lit: offline since the smoke emote played here;
// online since the server accepted it, by the server's clock (a refused one
// lights nothing).
bool cigarette_lit(const game_state& state);

// My row among the room's players, if the server sent it.
const room_player* my_room_player(const game_state& state);

// Called once state.round has the hole. Offline: the save's hole count, the
// course when the round is finished (not on practice courses), and a save
// request. Online the server records it from the holed shot.
void record_hole_completed(game_state& state);

// Online progress from the server. Skills that gained XP since the last
// snapshot show as XP drops; the first snapshot only sets where they start.
void receive_online_progress(game_state& state, const save_data& progress);
