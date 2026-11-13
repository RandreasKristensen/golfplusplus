#pragma once

// Online, in a group: each member's completed holes from their current
// round's server row, and whether my finished results wait for the group.
// Finished scores stay on that row until the member books a replacement round.
// My finished round shows its results once nobody else in my group is still
// playing the hole I finished on, and no shot of theirs is still to play;
// until then the room plays on around me. Offline and outside a group
// nothing here waits.

#include <cstdint>
#include <vector>

struct game_state;

// Per hole of the course, 0 until played: mine from my round, another
// member's from their current round's row. Shot history is never a score.
std::vector<int> member_round_strokes(const game_state& state, std::uint64_t account);

bool waiting_for_group(const game_state& state);
// My round is finished and the group is done with its last hole.
bool round_results_shown(const game_state& state);
