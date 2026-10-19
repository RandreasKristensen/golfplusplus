#pragma once

// The camcorder's datestamp (bottom left, with OFFLINE as its top line in
// offline play),
// the VCR's PLAY / PAUSE tag (top right, above the HUD and the menus' title)
// and the interact key's prompt (under the middle, in the datestamp's text),
// drawn into the overlay batch like burn-in: blocky, with a dark edge.
// GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"
#include "renderer/ui_rect.h"

// Where each is drawn, for the HUD to stay clear of.
ui_rect datestamp_box();
ui_rect vcr_tag_box();

// Nothing unless vcr.show_datestamp or vcr.show_offline.
void draw_datestamp(overlay_batch& batch, const text_assets& text, const render_vcr& vcr);
void draw_vcr_tag(overlay_batch& batch, const text_assets& text, const render_vcr& vcr);
void draw_interact_prompt(overlay_batch& batch, const text_assets& text);
