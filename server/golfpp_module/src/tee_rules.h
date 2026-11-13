#pragma once

// Authoritative UTC booking decisions, independent of database/connection state.
// Reducers validate before deleting an existing booking: refusal is atomic.
#include "game/game_tuning.h"
#include "game/tee_times.h"
#include <cstdint>
#include <string>
#include <glm/vec3.hpp>

inline constexpr std::uint32_t course_world_id = 1;
// A booked time's group is the slot's: golf_group rows share the slot's id.
inline std::uint64_t group_of_slot(const std::uint64_t slot_id) { return slot_id; }
bool tee_expired(std::int64_t scheduled, std::int64_t now, bool started, const server_tuning& tuning);
// An unstarted booking is missed once expired, or from its time while its
// golfer has not boarded: the bus leaves the hub at the tee time.
bool tee_missed(std::int64_t scheduled, std::int64_t now, bool started, bool boarded, const server_tuning& tuning);
std::string check_booking(std::uint64_t expected, std::uint64_t current, std::int64_t scheduled,
                          std::int64_t now, int occupied, bool same_slot, const server_tuning& tuning);
std::string check_bus(std::int64_t scheduled, std::int64_t now, bool started, const server_tuning& tuning);
// Before the tee time a group may go only when the first tee is clear: no
// other group still on hole 1 and no earlier tee time still waiting, as a
// starter would send a group off early.
std::string check_first_tee(std::int64_t scheduled, std::int64_t now, int hole, bool started, bool tee_clear,
                            const server_tuning& tuning);

// Group members use stable, separate positions along the first tee's left axis.
glm::vec3 booked_tee_position(const glm::vec3& tee, const glm::vec3& pin, std::uint32_t lane,
                               const game_tuning& tuning);
