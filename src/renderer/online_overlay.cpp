#include "renderer/online_overlay.h"

#include "game/text_ids.h"
#include "renderer/control_icons.h"
#include "renderer/hud_overlay.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <optional>

namespace {
// A tag's box around the projected point; the style sets how much it fills.
const glm::vec2 name_tag_half(0.20f, 0.026f);
// Bottom centre, between the power meter and the key icons.
const ui_rect notice_box = rect_from_edges(-0.26f, -0.86f, 0.60f, -0.70f);
}

void draw_name_tags(overlay_batch& batch,
                    const text_assets& text,
                    const std::vector<render_name_tag>& tags,
                    const glm::mat4& view_proj) {
    const text_style& style = find_text_style(text, style_name_tag);
    for (const render_name_tag& tag : tags) {
        const std::optional<glm::vec2> projected = project_to_screen(view_proj, tag.position);
        if (!projected || tag.name.empty()) {
            continue;
        }
        draw_label(batch, text.font, style, tag.name, ui_rect{*projected + glm::vec2(0.0f, name_tag_half.y), name_tag_half});
    }
}

void draw_notice(overlay_batch& batch, const text_assets& text, const std::string& label) {
    if (label.empty()) {
        return;
    }
    draw_overlay_quad(batch, notice_box.center, notice_box.half_size, glm::vec3(0.03f, 0.02f, 0.02f), 0.78f);
    draw_button_outline(batch, notice_box.center, notice_box.half_size, glm::vec3(0.62f, 0.30f, 0.22f), 0.60f);
    draw_label(batch, text.font, find_text_style(text, style_error), label, inset_rect(notice_box, glm::vec2(0.02f, 0.01f)));
}
