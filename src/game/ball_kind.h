#pragma once

// Why a ball is in play. Practice balls have no hole; warmup and scored balls do.
#include <cstdint>
enum class ball_kind : std::uint8_t { scored, warmup, practice };
