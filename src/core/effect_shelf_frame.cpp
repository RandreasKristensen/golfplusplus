#include "core/effect_shelf_frame.h"

#include "game/effect_shelf.h"
#include "game/effect_text.h"
#include "game/mode_dispatch.h"

#include <utility>

void add_effect_shelf(render_data& data, const game_state& game, const text_assets& text) {
    for (const effect_square& square : effect_shelf(current_effects(game), game.items, game.event_effects, shot_unix_ms(game))) {
        render_effect_square shown;
        shown.icon = square.icon;
        shown.harmful = square.harmful;
        if (square.seconds_left) {
            shown.seconds = effect_seconds_text(text, *square.seconds_left);
        }
        shown.name = lookup_text(text, square.name.c_str());
        if (!square.description.empty()) {
            shown.description = lookup_text(text, square.description.c_str());
        }
        for (const item_effect& effect : square.effects) {
            shown.changes.push_back(effect_change_text(text, effect));
        }
        data.effect_squares.push_back(std::move(shown));
    }
}
