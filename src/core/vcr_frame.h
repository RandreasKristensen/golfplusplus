#pragma once

// What the camcorder and the VCR add to a frame (render_vcr): the
// datestamp burned in from the sky clock's date and time on the course, with
// OFFLINE over it in offline play, the VCR's PLAY tag in the menus and PAUSE
// tag with a still frame while the game is paused. Looks only; nothing in the
// game reads it. GL-free.

#include "core/sky_frame.h"
#include "game/game_state.h"
#include "game/text_assets.h"
#include "renderer/render_data.h"

#include <cstdint>

// The datestamp at `game_day` on a clock `utc_offset_hours` ahead of UTC, as
// a 12-hour clock.
render_datestamp make_datestamp(const text_assets& text, double game_day, double utc_offset_hours);

struct vcr_signals {
    bool in_menu = false;    // the startup menus
    bool paused = false;     // the pause menu or the round's results
    bool datestamp = false;  // the setting
};

// `local_unix_ms` is this machine's UTC clock.
render_vcr make_render_vcr(const text_assets& text, const game_state& game, const sky_controls& controls,
                           std::int64_t local_unix_ms, const vcr_signals& signals);
