#pragma once

// The HUD's panel look, shared by every in-round panel (the club card, the
// swing clock, the god menu) and the rangefinder's amber. GL-free.

#include "renderer/overlay_batch.h"
#include "renderer/ui_rect.h"

#include <glm/vec3.hpp>

inline const glm::vec3 hud_panel_color(0.055f, 0.060f, 0.065f);
inline const glm::vec3 hud_outline_color(0.62f, 0.64f, 0.60f);
inline const glm::vec3 hud_amber(0.92f, 0.70f, 0.18f);

// A HUD panel: its drop shadow, face and outline.
void draw_hud_panel(overlay_batch& batch, const ui_rect& panel);
