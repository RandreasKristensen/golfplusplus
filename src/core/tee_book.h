#pragma once
// Physical tee book and bus menus. The times of a course come from the clock, so
// they show on the first frame; the server's counts fill in as its answer to the
// list request (core/tee_book_list.h) arrives, unknown until then. A booking or
// cancellation waits for its reservation row, one at a time; a refused
// replacement keeps the complete original reservation. Requests keep their
// originating room/reservation token.
#include "core/startup_flow.h"
#include "core/tee_book_list.h"
#include "game/game_state.h"
#include "game/message_severity.h"
#include "game/tee_times.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct tee_book_state {
    hub_screen screen = hub_screen::none;
    std::string course;
    // Cart or carry when no booking decides it (a booking's own choice is shown otherwise).
    bool takes_cart = true;
    int selection = 0;
    int top = 0;
    // The time selected, found again by it when the list moves as the clock does.
    std::int64_t selected_time = 0;
    tee_list_state list;
    // A booking or cancellation sent and not yet confirmed by its reservation row.
    std::optional<net_command_type> pending;
    std::int64_t scheduled = 0;
    // What the pending book_tee asked for: its course and whether it takes a cart.
    std::string pending_course;
    bool pending_cart = false;
    // A refused or impossible action, until the next one is tried: a text key.
    std::string message;
    message_severity severity = message_severity::error;
};
// Shows `key` as the book's error (a refusal), replacing any earlier message.
void set_book_error(tee_book_state& menu, const std::string& key);
std::vector<ui_sound> update_tee_book(tee_book_state& menu, game_state& game, const startup_catalog& catalog,
    const input_state& input, const menu_pointer& pointer, const std::vector<reducer_failure>& refusals);
render_startup_menu render_tee_book(const tee_book_state& menu, const game_state& game,
    const startup_catalog& catalog, const text_assets& text);
// A tee time (real UTC microseconds) as the hub's in-game clock reads it, like
// the camcorder datestamp, with TOMORROW before a time on the next in-game
// day. Never a real date or time.
std::string tee_clock_text(std::int64_t micros, const game_state& game, const text_assets& text);  // the time alone
std::string tee_time_text(std::int64_t micros, const game_state& game, const text_assets& text);
// The part of the day's text key for the sun `sun_elevation_deg` up, before
// or after its noon, and this many hours from it.
const char* part_of_day_key(float sun_elevation_deg, bool before_noon, float solar_hours_from_noon);
// The time on `course`'s own clock at `unix_ms` (its clock_shift_hours and
// date) and the part of its day, so a player can choose a sunrise: "6:12 AM
// SUNRISE".
std::string course_time_text(const course_definition& course, const game_state& game, const text_assets& text,
                             std::int64_t unix_ms);
// The hub's clock, and a catalog's course by id (nullptr when it has none).
hub_clock tee_book_clock(const game_state& game);
const course_definition* catalog_course(const startup_catalog& catalog, const std::string& id);
// A course card's figures (holes, par, length) and its detail line (the time there, the season, carts).
std::string course_figures_text(const startup_course_option& option, const text_assets& text);
std::string course_detail_text(const startup_course_option& option, const game_state& game, const text_assets& text,
                               std::int64_t unix_ms);
// A number with a comma between its thousands: 5912 reads "5,912".
std::string with_thousands(int value);
std::string course_name(const startup_catalog& catalog, const std::string& id);
std::string booking_text(const game_state& game, const startup_catalog& catalog, const text_assets& text);
// One per game.area.props: what its board says, empty for none. The tee book
// is titled; the bus shows where it goes (the booked course, or the hub); the
// clocks say `clock` (the hub's in-game time, as the datestamp reads it); a
// target's board its distance; the departure board's rows the next departures
// (core/departure_board.h).
std::vector<std::string> prop_board_labels(const game_state& game, const startup_catalog& catalog, const text_assets& text,
                                           const std::string& clock);
