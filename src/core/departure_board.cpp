#include "core/departure_board.h"

#include "core/tee_book.h"
#include "game/mode_dispatch.h"
#include "game/model_kinds.h"
#include "game/text_ids.h"
#include "renderer/hole_sign_face.h"

#include <string>

std::string departure_text(const departure_info& departure, const game_state& game, const startup_catalog& catalog,
                           const text_assets& text, const int face_width) {
    const std::string time = tee_time_text(departure.scheduled_at, game, text);
    const std::string course = course_name(catalog, departure.course_id);
    const std::string line = format_text(text, text_hub_departure_row, {{"time", time}, {"course", course}});
    std::string names;
    for (const std::string& name : departure.names) { names += (names.empty() ? "" : ", ") + name; }
    if (names.empty()) { return line; }
    if (prop_board_label_fits(text, line + "\n" + names, face_width)) { return line + "\n" + names; }
    const std::string first = departure.names.size() > 1
        ? format_text(text, text_hub_departure_more, {{"name", departure.names.front()}, {"more", std::to_string(departure.names.size() - 1)}})
        : departure.names.front();
    return prop_board_label_fits(text, line + "\n" + first, face_width) ? line + "\n" + first : line;
}

std::string departure_row_label(const game_state& game, const startup_catalog& catalog, const text_assets& text, const int row) {
    if (!is_online(game)) {
        const auto& booking = active_booking(game);
        if (row != 0 || !booking) { return {}; }
        return course_name(catalog, booking->course_id);
    }
    if (row < 0 || static_cast<std::size_t>(row) >= game.online.departures.size()) { return {}; }
    const auto found = game.hub_props.find(departure_row_model);
    const int width = found == game.hub_props.end() ? 1 : prop_board_face_width(glm::vec2(found->second.point("board_size")) * 0.5f);
    return departure_text(game.online.departures[static_cast<std::size_t>(row)], game, catalog, text, width);
}
