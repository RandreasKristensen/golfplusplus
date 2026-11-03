#include "game/gear_paint.h"

#include "game/game_state.h"

#include <algorithm>

std::size_t gear_paint_index(const gear_paint& paint, const std::size_t paint_count) {
    if (paint_count == 0) {
        return 0;
    }
    if (paint.favourite != 0) {
        return std::min<std::size_t>(paint.favourite - 1u, paint_count - 1);
    }
    return static_cast<std::size_t>(paint.account % paint_count);
}

gear_paint gear_paint_of(const game_state& state, const std::uint64_t owner) {
    const std::uint64_t me = state.online.account_id;
    if (owner == 0 || owner == me) {
        return gear_paint{state.favourite_colour, me};
    }
    const auto player = state.online.players.find(owner);
    return gear_paint{player != state.online.players.end() ? player->second.favourite_colour : std::uint8_t{0}, owner};
}
