#pragma once

// Keyboard key icons drawn from overlay segments: shared by the HUD controls
// overlay and the startup help screen. GL-free.

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "renderer/overlay_batch.h"

void draw_button_outline(overlay_batch& batch,
                         glm::vec2 center,
                         glm::vec2 half_size,
                         glm::vec3 color,
                         float alpha);
void draw_control_button_base(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size, bool is_down);
glm::vec3 control_icon_color(bool is_down);
float control_icon_alpha(bool is_down);

void draw_arrow_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 direction, glm::vec2 half_size, bool is_down);
void draw_space_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size, glm::vec3 color, float alpha);
void draw_space_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size, bool is_down);
void draw_shift_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size, bool is_down);
void draw_enter_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size, bool is_down);
void draw_backspace_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size, bool is_down);
void draw_retee_icon(overlay_batch& batch, glm::vec2 center, glm::vec2 half_size, bool is_down);
