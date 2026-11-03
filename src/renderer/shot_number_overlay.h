#pragma once

// My shot's number over my ball as it is struck, damage-number style: the
// number in its box above where the ball is seen, rising and fading, a
// one-font-pixel black edge round it so it reads against sky and grass; a
// crit in its own colour. GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"

#include <glm/mat4x4.hpp>

void draw_shot_number(overlay_batch& batch, const text_assets& text, const render_shot_number& number,
                      const glm::mat4& view_proj);
