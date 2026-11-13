#pragma once
// What the hub and a booked course let the player do, offline and online:
// warm up, prepare the first tee, and take the bus between them.
#include "game/game_state.h"
#include <cstddef>

bool course_warmup(const game_state& state);
bool first_tee_required(const game_state& state);
bool reservation_expired(const game_state& state);
void request_ball_preparation(game_state& state, net_command_type type, std::size_t hole = 0, bool give_up = false);
void request_bus_travel(game_state& state, bool takes_cart);
void clear_online_presentation(game_state& state);
// A warmup ball is on a practice green, so it is putted.
void select_putter(game_state& state);
