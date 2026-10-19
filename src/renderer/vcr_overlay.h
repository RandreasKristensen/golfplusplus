#pragma once

// The camcorder's datestamp (bottom left, with OFFLINE as its top line in
// offline play) and
// the VCR's PLAY / PAUSE tag (top right, above the HUD and the menus' title),
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
