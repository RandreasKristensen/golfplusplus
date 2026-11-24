#pragma once

// The effect shelf drawn into the overlay: a bordered square of art per
// running source with its seconds left under it (a debuff's border in the
// error red, a buff's in the HUD's amber), a `+N` square when the rows are
// full, and, while the freed mouse (right button on foot) is over a square,
// a tooltip in the menu text styles with its name, description and what it
// changes. Laid out by renderer/effect_shelf_layout.h. GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/pixel_font.h"
#include "renderer/render_data.h"
#include "renderer/ui_rect.h"

#include <optional>
#include <vector>

#include <glm/vec2.hpp>

void draw_effect_shelf(overlay_batch& batch, const text_assets& text, const std::vector<render_effect_square>& squares,
                       const std::optional<glm::vec2>& cursor);

// The tooltip's panel and texts, in overlay clip space.
struct effect_tooltip {
    ui_rect panel;
    text_layout name;
    text_layout description;  // no lines when there is none
    std::vector<text_layout> changes;
};

// `square` placed by `cursor`: down and right of it, below the hovered
// square's box so it never covers it, and inside the screen.
effect_tooltip layout_effect_tooltip(const text_assets& text, const render_effect_square& square, const ui_rect& hovered,
                                     glm::vec2 cursor, const overlay_grid& grid);

// Drawn last, over the rest of the HUD; nothing without the cursor over a square.
void draw_effect_tooltip(overlay_batch& batch, const text_assets& text, const std::vector<render_effect_square>& squares,
                         const std::optional<glm::vec2>& cursor);
