#pragma once

// Authoritative UTC booking decisions, independent of database/connection state.
// Reducers validate before deleting an existing booking: refusal is atomic.
#include "game/game_tuning.h"
#include "game/tee_times.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

inline constexpr std::uint32_t course_world_id = 1;
// A booked time's group is the slot's: golf_group rows share the slot's id.
inline std::uint64_t group_of_slot(const std::uint64_t slot_id) { return slot_id; }
bool tee_expired(std::int64_t scheduled, std::int64_t now, bool started, const server_tuning& tuning);
// An unstarted booking is missed once expired, or from its time while its
// golfer has not boarded: the bus leaves the hub at the tee time.
bool tee_missed(std::int64_t scheduled, std::int64_t now, bool started, bool boarded, const server_tuning& tuning);
// A booked time's bus has left once its time has come: it is off the board.
inline bool bus_gone(const std::int64_t scheduled, const std::int64_t now) { return now >= scheduled; }
// The choice stored with a booking: my own cart only where the course has them.
inline bool booked_takes_cart(const bool asked, const bool course_has_carts) { return asked && course_has_carts; }
// A booked time on the departure board.
struct board_slot {
    std::uint64_t slot_id = 0;
    std::int64_t scheduled_at = 0;
};
// The first `rows` of `waiting` (booked times whose bus has not left), earliest first, equal times by slot.
std::vector<board_slot> next_departures(std::vector<board_slot> waiting, std::size_t rows);
std::string check_booking(std::uint64_t expected, std::uint64_t current, std::int64_t scheduled,
                          std::int64_t now, int occupied, bool same_slot, const server_tuning& tuning, const hub_clock& clock);
std::string check_bus(std::int64_t scheduled, std::int64_t now, bool started, const server_tuning& tuning);
// Before the tee time a group may go only when the first tee is clear: no
// other group still on hole 1 and no earlier tee time still waiting, as a
// starter would send a group off early.
std::string check_first_tee(std::int64_t scheduled, std::int64_t now, int hole, bool started, bool tee_clear,
                            const server_tuning& tuning);

// Group members use stable, separate positions along the first tee's left axis.
glm::vec3 booked_tee_position(const glm::vec3& tee, const glm::vec3& pin, std::uint32_t lane,
                               const game_tuning& tuning);
