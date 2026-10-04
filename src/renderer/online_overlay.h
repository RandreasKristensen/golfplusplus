#pragma once

// The online HUD (renderer/hud_overlay.h draws it): name tags over other
// players, small and a bit fuzzy through the CRT like camcorder captions,
// and a refusal from the server for a moment. The group's scorecard is with
// the other scorecards (renderer/scorecard_overlay.h). GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

void draw_name_tags(overlay_batch& batch, const text_assets& text, const std::vector<render_name_tag>& tags, const glm::mat4& view_proj);
// Nothing when `label` is empty.
void draw_notice(overlay_batch& batch, const text_assets& text, const std::string& label);
