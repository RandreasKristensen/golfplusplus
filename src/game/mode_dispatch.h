#pragma once

// Where offline and online play part ways. Gameplay reports what the player
// did through these, and nothing else in game code writes progress or pushes
// net commands. Offline applies progress_rules to the local save at once;
// online pushes a net_command, and the result comes back from the server
// through receive_online_progress.

#include "game/course_world_definition.h"
#include "game/game_state.h"
#include "game/net_types.h"
#include "game/progress_rules.h"
#include "game/reward_rules.h"
#include "game/save_data.h"
#include "game/shot_simulation.h"
#include "game/weather.h"

#include <cstddef>
#include <cstdint>

bool is_online(const game_state& state);

// The progress the player sees and plays with: the local save offline, the
// server's online. Everything that shows or checks progress reads this.
const save_data& active_progress(const game_state& state);

// Me as a golfer: my hand, my items' effects on my spread, and my natural
// shape (offline the save's, online my player row's; neutral until picked).
golfer_profile my_golfer(const game_state& state);

// Whether I have picked my natural shape: offline in the save, online on
// my player row (once the server has sent it).
bool natural_shape_picked(const game_state& state);

// I picked my natural shape. Offline: the save's, written at the next save.
// Online: set_golfer, with my hand.
void record_natural_shape(game_state& state, natural_shape shape);

// Online, my player row keeps my hand as my settings have it: set_golfer
// when it differs (once per change). Offline nothing.
void sync_golfer(game_state& state);

// The dice of the shot I hit next offline: the round's next (round_shot_seed).
std::uint64_t next_shot_seed(const game_state& state);

// I swung at my ball as `setup`, its next stroke. Offline: swing XP.
// Online: take_shot with the flags as I have them; the server answers with
// the shot it played (its event), or refuses it.
void record_shot(game_state& state, const shot_setup& setup);

// I took green `hole`'s flag out (`out`) or put it back. Offline: at once.
// Online: set_flag; the room's rows say when it is.
void record_flag(game_state& state, std::size_t hole, bool out);

// I moved my bag to where state.bag now has it, or took `club_id` from it
// into my hand. Online: place_bag, take_club. Offline nothing more.
void record_bag_moved(game_state& state);
void record_club_taken(game_state& state, const std::string& club_id);

// I used `item` at the shared clock's now. Offline: its XP per use and its
// timed effects. Online: use_item, which the server checks and answers with
// the same.
void record_item_use(game_state& state, const item_definition& item);

// What I have equipped: the save's offline, my player_loadout row's online
// (the defaults until it arrives), each slot the default's until it names an
// item of its kind.
loadout equipped_loadout(const game_state& state);

// Every item effect on me now: what I equipped, and the timed effects of what
// I used (offline mine, online the server's).
std::vector<active_effect> current_effects(const game_state& state);
// The same with the shared clock's now, for an XP award.
xp_effects current_xp_effects(const game_state& state);

// The player walked or drove `meters`. Offline: movement XP at `rate`, the
// leftover kept in `pending_meters`. Online the server counts movement from
// motion updates, so nothing happens here.
void record_movement(game_state& state, const movement_xp_rate& rate, float& pending_meters, float meters);

// Offline: claims it on the save. Online: claim_collectible.
void record_collectible_claim(game_state& state, const course_world_collectible& collectible);

// The holed ball was picked out of its cup. Online: pick_up_ball, so the
// server lets the next hole start too. Offline needs nothing.
void record_ball_picked_up(game_state& state);

// My ball was teed up on `hole_index`, giving up the hole it was in play on
// when `give_up`. Online: tee_up. Offline needs nothing.
void record_tee_up(game_state& state, std::size_t hole_index, bool give_up);

// The hole my ball was in play on was given up (no score). Online:
// give_up_hole. Offline needs nothing.
void record_hole_given_up(game_state& state);

// Stepping into the passenger seat of `owner`'s cart, and out of it again.
// Online: board_cart and leave_cart, which the server checks (reach, a free
// seat). Offline there is only my own cart: nothing.
void record_cart_boarded(game_state& state, std::uint64_t owner);
void record_cart_left(game_state& state);

// My cart tipped onto a side or sank under me, and someone's tipped cart
// (`owner`, 0 mine) was stood back up. Online: overturn_cart and
// right_cart, which the server checks plausible where it has the cart.
// Offline the game does it all: nothing.
void record_cart_overturned(game_state& state, cart_condition condition);
void record_cart_righted(game_state& state, std::uint64_t owner);

// G: join the group of the nearest player (net.group_join_distance) when it
// has space, else start a group. Shift+G leaves. Online only.
void request_group(game_state& state);
void request_leave_group(game_state& state);

// UTC now in milliseconds this frame, as shared_unix_ms has it from the
// state's local_unix_ms: what a shot hit now records as its shot_at, which
// the server checks against its own clock.
std::int64_t shot_unix_ms(const game_state& state);

// The weather on my course at `unix_ms`: offline the god menu's when it holds
// one (weather_override), else the course's own (game/weather.h), which is
// what the server plays online shots in.
weather_state course_weather_at(const game_state& state, std::int64_t unix_ms);

// UTC now in milliseconds, as every player in the room has it: offline the
// local clock (`local_unix_ms`), online the server's clock once known. The
// sky is drawn from it (game/sky_clock.h).
std::int64_t shared_unix_ms(const game_state& state, std::int64_t local_unix_ms);

// My row among the room's players, if the server sent it.
const room_player* my_room_player(const game_state& state);

// Called once state.round has the hole. Offline: the save's hole count, the
// course when the round is finished, and a save request. Online the server
// records it from the holed shot.
void record_hole_completed(game_state& state);

// Online progress from the server. Skills that gained XP since the last
// snapshot show as XP drops; the first snapshot only sets where they start.
void receive_online_progress(game_state& state, const save_data& progress);
