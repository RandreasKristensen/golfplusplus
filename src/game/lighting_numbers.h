#pragma once

// A number of lighting.json found by its path ("weather/sun_glow",
// "keys/2/mist_density"), as a loaded palette holds it, with the range the
// loader allows it (game/lighting_fields.h): what the live tuning panel
// (core/look_tuning.h) steps and clamps.

#include "game/json_edit.h"
#include "game/lighting_loader.h"

#include <cstddef>
#include <optional>
#include <vector>

struct lighting_number {
    float value = 0.0f;
    float low = 0.0f;
    float high = 0.0f;
    bool whole = false;  // a count, written without a fraction
};

// nullopt for a path that is not one of the loader's ranged numbers (a
// colour, a power-of-two resolution or a key out of range).
std::optional<lighting_number> lighting_number_at(const lighting_palette& palette, const json_path& path);

// The palette key whose sun elevation is nearest `sun_elevation_deg`.
std::size_t nearest_lighting_key(const std::vector<lighting_key>& keys, float sun_elevation_deg);
