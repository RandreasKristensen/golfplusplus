#pragma once

// Online, in a group: each member's strokes this round as this client has
// seen them, and whether my finished round's results wait for the group.
// The server clears a member's round_strokes as their round finishes, so
// their holes are kept here as they arrive (round_strokes and holed shots).
// My finished round shows its results once nobody else in my group is still
// playing the hole I finished on, and no shot of theirs is still to play;
// until then the room plays on around me. Offline and outside a group
// nothing here waits.

#include <cstdint>
#include <vector>

struct game_state;
struct room_shot;

// Every room member's played holes from their round_strokes, kept.
void note_round_strokes(game_state& state);
// A holed shot is the score of its hole.
void note_holed_shot(game_state& state, const room_shot& shot);
// Per hole of the course, 0 until played: mine from my round, another
// member's from what this round has seen of theirs.
std::vector<int> member_round_strokes(const game_state& state, std::uint64_t account);

bool waiting_for_group(const game_state& state);
// My round is finished and the group is done with its last hole.
bool round_results_shown(const game_state& state);
