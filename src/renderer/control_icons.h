#pragma once

// Keyboard key icons drawn from overlay segments, for the startup help
// screen, and the button outline panels share. GL-free.

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "renderer/overlay_batch.h"

void draw_button_outline(overlay_batch& batch,
                         glm::vec2 center,
                         glm::vec2 half_size,
                         glm::vec3 color,
                         float alpha);
// A key's face: a faint fill and its outline.
void draw_key_button(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size);

// Symbols drawn on a key's face.
void draw_arrow_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 direction, glm::vec2 half_size);
void draw_space_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size);
void draw_shift_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size);
