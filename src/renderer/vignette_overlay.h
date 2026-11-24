#pragma once

// The coloured edge of a screen vignette (game/vignette_strength.h) in the
// overlay, so the tape and the TV take it like the HUD: four slanted strips
// round the screen, the colour at `strength` times vignette_edge_alpha at the
// edge and clear at vignette_inset of the way to the middle. GL-free.

#include "renderer/overlay_batch.h"

#include <glm/vec3.hpp>

inline constexpr float vignette_edge_alpha = 0.75f;
inline constexpr float vignette_inset = 0.4f;  // overlay clip units from each edge

void draw_vignette(overlay_batch& batch, glm::vec3 colour, float strength);
