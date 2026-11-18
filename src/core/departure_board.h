#pragma once
// What the hub's departure board rows say. Online they are the server's
// next booked groups (departure_board): hub-clock time, course and who is in
// each, fitted to the row; offline the booking, if any.
#include "core/startup_flow.h"
#include "game/game_state.h"

// One departure on a row of width `face_width` texels (renderer/hole_sign_face.h):
// "TIME COURSE" over "NAME, NAME" when it all fits, else the first name and how
// many more, else the time and course alone (a long course name is cut by the board).
std::string departure_text(const departure_info& departure, const game_state& game, const startup_catalog& catalog,
                           const text_assets& text, int face_width);
// The text of row `row` (0 is the top), empty for none.
std::string departure_row_label(const game_state& game, const startup_catalog& catalog, const text_assets& text, int row);
