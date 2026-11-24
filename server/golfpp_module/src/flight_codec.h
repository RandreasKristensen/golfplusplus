#pragma once

// A live flight (flight_rules.h) as bytes for its row: every number as its
// exact bits, so a flight decoded is the flight encoded to the bit and the
// server resumes it as it left it. The shot's record, golfer and contacts are
// in it too. Plain C++ for the reducers and the native tests.

#include "flight_rules.h"

#include <cstdint>
#include <optional>
#include <vector>

std::vector<std::uint8_t> encode_flight(const ball_flight& flight);
// nullopt when `bytes` is not a whole flight.
std::optional<ball_flight> decode_flight(const std::vector<std::uint8_t>& bytes);

std::vector<std::uint8_t> encode_live_flight(const live_flight& live);
std::optional<live_flight> decode_live_flight(const std::vector<std::uint8_t>& bytes);
