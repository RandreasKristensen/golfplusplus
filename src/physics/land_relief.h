#pragma once

// The course's land with its relief scaled before the ground is built on it:
// the height data reads flatter than the real land feels, so the land's
// large-scale shape (its hills and valleys over a hole's length) is
// stretched about its mean height, while the detail on it (humps and mounds,
// the land minus its shape) is kept as it is. Greens are kept flatter about
// their own mean (a green's tilt and breaks are what make putts hard). Pure.

#include "physics/ground_mesh.h"
#include "physics/material_zone.h"

#include <vector>

struct land_relief {
    float scale = 0.0f;  // the land's shape about its mean, times this
    // The shape is the land averaged over about this width (a moving average
    // this wide, three times over: a bell about half as wide either side).
    float shape_meters = 0.0f;
    // On a green: its height about the green's mean, times this, of the
    // land as it was (not of the scaled land).
    float green_scale = 0.0f;
    float green_blend_distance = 0.0f;  // beyond a green's edge, back to the scaled land
};

// `land`'s shape: each height averaged over `width` metres along rows and
// columns, three times; past the grid's edge the land carries on as it
// slopes there, so a plane is its own shape.
height_grid land_shape(const height_grid& land, float width);

// `land` with `relief` applied; the green zones among `zones` (course
// coordinates) are the greens. Scales of 1 return the heights unchanged.
height_grid relieved_land(const height_grid& land, const std::vector<material_zone>& zones, const land_relief& relief);
