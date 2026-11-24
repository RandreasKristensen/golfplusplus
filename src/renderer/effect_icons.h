#pragma once

// The effect shelf's square art (renderer/effect_shelf_overlay.h): small
// pixel pictures drawn from overlay quads, named by an event effect's `icon`
// or an item's kind. An icon the renderer cannot draw is a content error,
// found by tests through effect_icon_known. GL-free.

#include "renderer/overlay_batch.h"
#include "renderer/ui_rect.h"

#include <string>

// Cells across and down every icon.
inline constexpr int effect_icon_cells = 10;

// Whether `icon` is drawn: "dazed", an item kind ("smokable", "drinkable"),
// or "plain", which every other item kind is drawn as.
bool effect_icon_known(const std::string& icon);

// `icon` filling `box`, a cell being a tenth of it, one UI pixel in the shelf. Draws nothing for an
// unknown icon.
void draw_effect_icon(overlay_batch& batch, const std::string& icon, const ui_rect& box);
