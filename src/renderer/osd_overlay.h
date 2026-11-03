#pragma once

// The camcorder-style OSD at address and walking, drawn into the overlay
// batch on the tape target's own pixels: the shot preview's start line as a
// crisp stepped line, white with a one-pixel black edge so it reads in day,
// night, rain and fog, fading out where it ends; the setup's word; the swing clock (its hand at the
// backswing, an overswing band past 12); and a pictogram of where my bag
// is. GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

// A line of whole target pixels from `a` to `b` (overlay clip space): a
// black edge one pixel round it, then the line itself.
void draw_osd_line(overlay_batch& batch, glm::vec2 a, glm::vec2 b);

// The preview over the world as `view_proj` sees it: data.osd_line, fading
// out towards its end.
void draw_shot_preview(overlay_batch& batch, const render_data& data, const glm::mat4& view_proj);

// Above the swing clock.
void draw_setup_word(overlay_batch& batch, const text_assets& text, const std::string& word);

// Bottom right: a quarter clock, 6 at address round to 12 at a full
// backswing, an overswing band past 12 up to `backswing_max`, the
// backswing filled in and the club as its hand there, or at `meter` (on
// past the backswing); red when `mistimed`.
void draw_swing_clock(overlay_batch& batch, const text_assets& text, float backswing, float backswing_max,
                      std::optional<float> meter, bool mistimed);

// Each of the green's read dots as one OSD pixel where `view_proj` sees it.
void draw_green_read(overlay_batch& batch, const std::vector<render_read_dot>& dots, const glm::mat4& view_proj);

// Each world segment as an OSD line, where `view_proj` sees it.
void draw_world_segments(overlay_batch& batch, const std::vector<std::pair<glm::vec3, glm::vec3>>& segments,
                         const glm::mat4& view_proj);

// Beside the club label: my bag on a cart, on a back or stood on the ground.
void draw_bag_pictogram(overlay_batch& batch, bag_pictogram place);
