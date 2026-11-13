#pragma once

// A bag or club being handled, mine and others' alike: a bag carried from
// where it was to where it goes (set down, taken up, strapped onto or off a
// cart) over player.bag_handle_seconds, and the club in hand put back in
// the bag and another drawn out over player.club_swap_seconds. The rules
// move a bag or a club at once (game/golf_bag.h, the server's player_bag
// row); this is how it is seen happening, without stopping the golfer walking.

#include "game/bag_place.h"

#include <optional>
#include <string>

struct game_state;
struct player_tuning;

enum class handling_kind {
    bag,  // the bag from one place to another
    club  // the club in hand back in the bag, another out of it
};

// A bag on its way is lifted this high (world units) over the straight way
// between its places, at the middle of the move.
inline constexpr float bag_arc_height = 0.35f;

struct bag_handling {
    handling_kind kind = handling_kind::bag;
    // bag: where it was and where it goes; club: where it stands.
    bag_place from;
    bag_place to;
    // club: the club put back, and the one drawn (club ids).
    std::string club_out;
    std::string club_in;
    float elapsed = 0.0f;
};

// How long `handling` takes.
float handling_seconds(const bag_handling& handling, const player_tuning& tuning);
// How far into it, 0 to 1.
float handling_progress(const bag_handling& handling, const player_tuning& tuning);

// Starts handling my bag from `from` to where it now is; and the club
// `club_out` swapped for the one in hand.
void start_bag_handling(game_state& state, const bag_place& from);
void start_club_swap(game_state& state, const std::string& club_out);

void update_bag_handling(game_state& state, float dt);

// Steps `handling` on by `dt`; nullopt once it has played.
std::optional<bag_handling> handling_after(const bag_handling& handling, const player_tuning& tuning, float dt);
