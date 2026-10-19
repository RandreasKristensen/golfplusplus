#pragma once

// The lighting palette (game/lighting_loader.h) evaluated at a sun
// elevation: the light, and how far the stars, the sun's shadows and the
// night lights show.

#include "game/lighting_loader.h"

// The light at `sun_elevation_deg`: blended between the keys either side,
// the first or last key beyond them. Continuous in the elevation.
lighting_key lighting_at(const lighting_palette& palette, float sun_elevation_deg);

// How much the stars show with the sun at `sun_elevation_deg`: 0 above
// stars_appear_deg, 1 below stars_full_deg, smooth between.
float star_visibility(const sky_style& sky, float sun_elevation_deg);

// How much of their shadow the sun casts at `sun_elevation_deg`: 0 below
// sun_fade_start_deg, 1 above sun_fade_full_deg, smooth between.
float sun_shadow_share(const shadow_style& shadows, float sun_elevation_deg);

// How far the night lights are on at `sun_elevation_deg`: 0 above on_deg, 1
// below full_deg, smooth between.
float night_light_share(const night_light_style& lights, float sun_elevation_deg);

// `day`, with the ball's and the ember's floors risen towards the night
// lights' as far as `night_share` (0..1).
glow_floors night_glow_floors(const glow_floors& day, const night_light_style& lights, float night_share);
