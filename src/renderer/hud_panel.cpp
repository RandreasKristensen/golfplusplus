#include "renderer/hud_panel.h"

#include "renderer/control_icons.h"

#include <glm/vec2.hpp>

void draw_hud_panel(overlay_batch& batch, const ui_rect& panel) {
    draw_overlay_quad(batch, panel.center + glm::vec2(0.012f, -0.014f), panel.half_size, glm::vec3(0.0f), 0.22f);
    draw_overlay_quad(batch, panel.center, panel.half_size, hud_panel_color, 0.80f);
    draw_button_outline(batch, panel.center, panel.half_size, hud_outline_color, 0.54f);
}
