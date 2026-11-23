#pragma once
// The tee book's list fetch: asking the server for a course's booked counts and
// following its answer, apart from booking and cancelling. The times are listed
// from the clock (game/tee_times.h) whether or not a fetch is under way, so
// browsing, switching courses and backing out never wait on one; a fetch only
// fills in the counts. One request is in flight at a time, and its wait is
// bounded: an answer, a refusal or the timeout ends it, and a failed one is
// retried only on request. Mirrors game_state::online's book_* fields, which
// the network client fills from the latest request's answer alone.
#include "game/game_state.h"

#include <cstdint>
#include <string>

enum class list_phase { idle, loading, failed };

struct tee_list_state {
    list_phase phase = list_phase::idle;
    std::string course;  // what the latest request asked for
    // online.book_answers and book_refusals when it was sent: an answer or a
    // refusal since then is its own.
    std::uint64_t answers_before = 0;
    std::uint64_t refusals_before = 0;
    std::int64_t requested_at = 0;  // server clock, 0 until the clock is known
    std::int64_t refreshed_at = 0;  // server clock, when the counts were last asked for or confirmed
    std::string error;              // the text key of why a failed request failed
};

// How far the counts shown for a course can be trusted.
enum class counts_state {
    unknown,    // never confirmed for this course: not zero, not available
    confirmed,  // the server's answer, still followed
    updating,   // the last confirmed counts while a request is under way
    old         // the last confirmed counts, which the server no longer follows or whose refresh failed
};

// Asks for `course`'s counts. Online without a complete room to send from,
// the request fails at once.
void request_list(tee_list_state& list, game_state& game, const std::string& course);
// Follows the answer, refusal or timeout of a request under way; otherwise asks
// again when `refresh` (a retry too) or the refresh interval has passed.
void update_list(tee_list_state& list, game_state& game, bool refresh);
counts_state counts_of(const tee_list_state& list, const game_state& game, const std::string& course);
