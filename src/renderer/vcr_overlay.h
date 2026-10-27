#pragma once

// The camcorder's datestamp (bottom left, with OFFLINE as its top line in
// offline play),
// the VCR's PLAY / PAUSE tag (top right, above the HUD and the menus' title)
// the interact key's prompt (under the middle, in the datestamp's text) and,
// over the ball, the strip of setup keys along the bottom (at my bag, its
// keys, and its club picker down the right with its own keys),
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
// Nothing when `label` is empty.
void draw_give_up_prompt(overlay_batch& batch, const text_assets& text, const std::string& label);
void draw_address_hints(overlay_batch& batch, const text_assets& text);
void draw_bag_hints(overlay_batch& batch, const text_assets& text);
// The clubs in bag order down the right, the highlighted one pointed at and
// the one in hand starred, the picker's keys along the bottom.
void draw_club_picker(overlay_batch& batch, const text_assets& text, const render_club_picker& picker);
