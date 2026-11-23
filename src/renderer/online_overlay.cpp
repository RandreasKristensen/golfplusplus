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
// Bottom centre, left of the swing clock.
const ui_rect notice_box = rect_from_edges(-0.34f, -0.86f, 0.42f, -0.70f);
// The notice's panel: warm for an error, the menus' neutral grey otherwise.
const glm::vec3 notice_error_fill(0.03f, 0.02f, 0.02f);
const glm::vec3 notice_error_border(0.62f, 0.30f, 0.22f);
const glm::vec3 notice_status_fill(0.02f, 0.022f, 0.022f);
const glm::vec3 notice_status_border(0.50f, 0.52f, 0.48f);

const char* name_tag_style(const player_relationship relationship) {
    switch (relationship) {
    case player_relationship::grouped:
        return style_name_tag_group;
    case player_relationship::unknown:
        break;
    }
    return style_name_tag;
}
}

void draw_name_tags(overlay_batch& batch,
                    const text_assets& text,
                    const std::vector<render_name_tag>& tags,
                    const glm::mat4& view_proj) {
    for (const render_name_tag& tag : tags) {
        const text_style& style = find_text_style(text, name_tag_style(tag.relationship));
        const std::optional<glm::vec2> projected = project_to_screen(view_proj, tag.position);
        if (!projected || tag.name.empty()) {
            continue;
        }
        draw_label(batch, text.font, style, tag.name, ui_rect{*projected + glm::vec2(0.0f, name_tag_half.y), name_tag_half});
    }
}

void draw_notice(overlay_batch& batch, const text_assets& text, const std::string& label, const message_severity severity) {
    if (label.empty()) {
        return;
    }
    // Only a refusal or failure is red: its text, its border and its background.
    const bool error = severity == message_severity::error;
    draw_overlay_quad(batch, notice_box.center, notice_box.half_size, error ? notice_error_fill : notice_status_fill, 0.78f);
    draw_button_outline(batch, notice_box.center, notice_box.half_size, error ? notice_error_border : notice_status_border, 0.60f);
    draw_label(batch, text.font, find_text_style(text, error ? style_error : style_subtitle), label,
               inset_rect(notice_box, glm::vec2(0.02f, 0.01f)));
}
