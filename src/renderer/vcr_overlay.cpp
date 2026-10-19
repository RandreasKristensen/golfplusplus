#include "renderer/vcr_overlay.h"

#include "game/text_ids.h"
#include "renderer/pixel_font.h"

#include <string>

#include <glm/vec2.hpp>

namespace {
// The text with its edge one font pixel down and right, both inside `box`.
void draw_burned_in(overlay_batch& batch, const text_assets& text, const text_style& style, const std::string& label,
                    const ui_rect& box) {
    const text_layout sized = layout_text(text.font, style, label, box, batch.grid);
    const glm::vec2 edge(sized.cell.x, -sized.cell.y);
    const ui_rect inner = rect_from_edges(rect_left(box), rect_bottom(box) - edge.y, rect_right(box) - edge.x, rect_top(box));
    const text_layout layout = layout_text_at_scale(text.font, style, label, inner, batch.grid, sized.scale);
    text_layout shadow = layout;
    for (text_line& line : shadow.lines) {
        line.top_left += edge;
    }
    shadow.truncated = false;
    draw_text_layout(batch, text.font, shadow, find_text_style(text, style_burn_in_shadow).color);
    draw_text_layout(batch, text.font, layout, style.color);
}
}

ui_rect datestamp_box() {
    return rect_from_edges(-0.92f, -0.95f, -0.38f, -0.65f);
}

ui_rect vcr_tag_box() {
    return rect_from_edges(0.40f, 0.905f, 0.92f, 0.975f);
}

void draw_datestamp(overlay_batch& batch, const text_assets& text, const render_vcr& vcr) {
    std::string lines = vcr.show_offline ? lookup_text(text, text_camcorder_offline) : "";
    if (vcr.show_datestamp) {
        lines += (lines.empty() ? "" : "\n") + vcr.datestamp.time + "\n" + vcr.datestamp.date;
    }
    if (lines.empty()) {
        return;
    }
    draw_burned_in(batch, text, find_text_style(text, style_datestamp), lines, datestamp_box());
}

void draw_vcr_tag(overlay_batch& batch, const text_assets& text, const render_vcr& vcr) {
    if (vcr.tag == vcr_tag::none) {
        return;
    }
    draw_burned_in(batch, text, find_text_style(text, style_vcr_tag),
                   lookup_text(text, vcr.tag == vcr_tag::play ? text_vcr_play : text_vcr_pause), vcr_tag_box());
}
