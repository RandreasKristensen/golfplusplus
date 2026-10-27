#pragma once

// The online parts of a frame (core/render_frame.h): other players, their
// balls, bags and clubs, shot trails and name tags, the server's notices and
// "waiting for group". Nothing is added offline. GL-free.

#include "game/game_state.h"
#include "game/text_assets.h"
#include "renderer/render_data.h"

#include <cstdint>
#include <optional>
#include <string>

void add_online_render_data(render_data& data, const game_state& game, const text_assets& text);

// Another player's bag where their player_bag row has it, and its model
// (their loadout's bag); none online without a row, or offline.
struct shown_bag {
    bag_place place;
    std::string model;
};
std::optional<shown_bag> remote_bag(const game_state& game, std::uint64_t account);
