#pragma once
// Physical tee book and bus menus. Reads complete acknowledged lists and
// reservation rows; requests keep their originating room/reservation token.
#include "core/startup_flow.h"
#include "game/game_state.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct tee_book_state {
    hub_screen screen = hub_screen::none;
    std::string course;
    int selection = 0;
    int top = 0;
    std::optional<net_command_type> pending;
    std::int64_t scheduled = 0;
    std::int64_t refreshed_at = 0;
    std::string message;
};
std::vector<ui_sound> update_tee_book(tee_book_state& menu, game_state& game, const startup_catalog& catalog,
    const input_state& input, const menu_pointer& pointer, const std::vector<reducer_failure>& refusals);
render_startup_menu render_tee_book(const tee_book_state& menu, const game_state& game,
    const startup_catalog& catalog, const text_assets& text);
// A tee time (real UTC microseconds) as the hub's in-game clock reads it, like
// the camcorder datestamp; a later in-game day than now says how many days on.
// Never a real date or time.
std::string tee_time_text(std::int64_t micros, const game_state& game, const text_assets& text);
std::string booking_text(const game_state& game, const startup_catalog& catalog, const text_assets& text);
// One per game.area.props: what its board says, empty for none. The tee book
// is titled; the bus shows where it goes (the booked course, or the hub).
std::vector<std::string> prop_board_labels(const game_state& game, const startup_catalog& catalog, const text_assets& text);
