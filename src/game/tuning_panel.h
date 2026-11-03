#pragma once

// The numbers of lighting.json the live tuning panel (core/look_tuning.h)
// steps, from assets/picture/tuning_panel.json: each a label (a text id,
// its {elevation} the palette key's), a path into lighting.json and a step.
// A path segment sun_key_segment stands for the palette key nearest the sun
// shown. Stepping edits the file's text in place (game/json_edit.h), within
// the loader's range, and only to a text the loader takes. GL-free.

#include "game/json_edit.h"
#include "game/lighting_loader.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* tuning_panel_path = "picture/tuning_panel.json";
inline constexpr const char* sun_key_segment = "@sun";
// A fine step is this share of an entry's step (a whole number's stays 1).
inline constexpr double fine_step_share = 0.1;

struct tuning_entry {
    std::string label;
    json_path path;
    double step = 0.0;
};

// nullopt unless every entry has a label, a positive step and a path to
// one of `palette`'s ranged numbers (lighting_numbers.h).
std::optional<std::vector<tuning_entry>> parse_tuning_panel_from_text(const std::string& text,
                                                                      const lighting_palette& palette);
std::optional<std::vector<tuning_entry>> load_tuning_panel(const std::string& asset_root, const lighting_palette& palette);

// `entry`'s path with the sun's key as `key`.
json_path tuning_entry_path(const tuning_entry& entry, std::size_t key);

// The step a press takes: the entry's, or with `fine` a tenth of it.
double tuning_step(const tuning_entry& entry, bool whole, bool fine);

// The places the panel shows `entry` with: enough for a fine step.
int tuning_decimals(const tuning_entry& entry, bool whole);

// lighting.json's `text` with the number at `path` moved `direction` (+1
// or -1) steps of tuning_step, clamped to the loader's range and rounded
// to the step's places (or the number's, if it has more). nullopt when the
// text does not load, nothing is there, the number would not change or the
// loader refuses the result.
std::optional<std::string> step_lighting_text(const std::string& text, const tuning_entry& entry, const json_path& path,
                                              int direction, bool fine);
