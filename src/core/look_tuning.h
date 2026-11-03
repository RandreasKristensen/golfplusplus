#pragma once

// Tuning the look live: lighting.json and picture.json reloaded when saved
// (game/look_reload.h), and the F2 panel (in the visual tour, with the god
// menu enabled) listing the most-tuned numbers of lighting.json
// (game/tuning_panel.h). Up and Down pick a row, Left and Right step it
// (Shift: a tenth of a step); each step is written into lighting.json,
// changing only that number's text, and shown at once, so the reload finds
// nothing new. A mist row tunes the palette key nearest the sun shown. A
// file that fails to load, or a step that cannot be written, is shown in
// the notice until it is put right. Drawn as the god menu's panel. GL-free.

#include "core/input.h"
#include "game/lighting_loader.h"
#include "game/look_reload.h"
#include "game/text_assets.h"
#include "game/tuning_panel.h"
#include "renderer/god_menu_overlay.h"
#include "renderer/render_data.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct look_tuning_state {
    look_watch watch;
    std::vector<tuning_entry> entries;
    bool open = false;
    std::size_t selected = 0;
    float sun_elevation_deg = 0.0f;  // the sun shown last frame
    bool unwritten = false;          // the last step could not be written
};

struct look_tuning_keys {
    bool toggle = false;  // F2
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool fine = false;  // Shift held
};

look_tuning_keys look_tuning_keys_of(const input_state& input);

// The panel after `keys`: open only while `allowed`; Up and Down move the
// row, wrapping. The step Left (-1) or Right (+1) asks for, 0 for none.
int update_look_panel(look_tuning_state& state, const look_tuning_keys& keys, bool allowed);

// This frame: the panel's keys, then (without a step) the files' poll. The
// look to show when it changed; `shown` is the palette shown now.
std::optional<look_reload> run_look_tuning(look_tuning_state& state, const look_tuning_keys& keys, bool allowed,
                                           const lighting_palette& shown, float dt);

// The open panel: each entry's label and its number in `shown`.
render_god_menu look_tuning_view(const look_tuning_state& state, const lighting_palette& shown, const text_assets& text);

// Why the files on disk are not shown or a step was not written; empty
// when neither.
std::string look_notice(const look_tuning_state& state, const text_assets& text);

// `data` with the open panel and the notice; notes the sun it shows for
// the next step.
void show_look_tuning(render_data& data, look_tuning_state& state, const lighting_palette& shown, const text_assets& text);
