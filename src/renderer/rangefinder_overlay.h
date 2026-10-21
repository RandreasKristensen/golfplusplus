#pragma once

// The rangefinder held up (game/rangefinder.h), as through a golf laser
// rangefinder's eyepiece: the eyepiece's outline round the magnified view
// (renderer/eyepiece_pass.h), an LCD aiming circle with crosshairs at the centre and a lock ring round it on the flag,
// brackets on the flagstick, the flag lock above, the distance and the
// height below and the wind lower left like a status icon. GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"

#include <glm/mat4x4.hpp>

// The eyepiece's radius, a share of half the picture's height; its outline
// is drawn this many pixels wide outside it.
inline constexpr float eyepiece_radius_share = 0.90f;
inline constexpr int eyepiece_outline_pixels = 3;

void draw_rangefinder(overlay_batch& batch, const text_assets& text, const render_data& data, const glm::mat4& view_proj);
