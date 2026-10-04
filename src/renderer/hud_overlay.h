#pragma once

// The in-round HUD drawn into the overlay batch: club, power meter, cart,
// rangefinder, XP drops, skills panel, key icons, the mode and room line, the
// compact scorecard (renderer/scorecard_overlay.h) and the online HUD
// (renderer/online_overlay.h). GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Everything except the course map (drawn by the renderer from its retained
// buffer), the startup menus and the debug text.
void draw_hud(overlay_batch& batch, const text_assets& text, const render_data& data, const glm::mat4& view_proj);
// Overlay clip-space position of a world point, or nullopt when it is
// behind the camera or outside the depth range.
std::optional<glm::vec2> project_to_screen(const glm::mat4& view_proj, const glm::vec3& world_position);

// FPS and profiling lines (developer diagnostics, not from the string table).
void draw_debug_text(overlay_batch& batch, const text_assets& text, const render_data& data);
