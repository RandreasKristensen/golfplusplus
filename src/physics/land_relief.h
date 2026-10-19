#pragma once

// The course's land with its relief scaled before the ground is built on it:
// the height data reads flatter than the real land feels, so hills are
// stretched about the land's mean height, and greens are kept flatter about
// their own mean (a green's tilt and breaks are what make putts hard). Pure.

#include "physics/ground_mesh.h"
#include "physics/material_zone.h"

#include <vector>

struct land_relief {
    float scale = 0.0f;  // the land's height about its mean, times this
    // On a green: its height about the green's mean, times this, of the
    // land as it was (not of the scaled land).
    float green_scale = 0.0f;
    float green_blend_distance = 0.0f;  // beyond a green's edge, back to the scaled land
};

// `land` with `relief` applied; the green zones among `zones` (course
// coordinates) are the greens. Scales of 1 return the heights unchanged.
height_grid relieved_land(const height_grid& land, const std::vector<material_zone>& zones, const land_relief& relief);
