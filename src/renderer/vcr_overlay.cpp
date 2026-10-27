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

void draw_interact_prompt(overlay_batch& batch, const text_assets& text) {
    const text_style& style = with_align(find_text_style(text, style_datestamp), text_align::center);
    draw_burned_in(batch, text, style, lookup_text(text, text_controls_key_f), ui_rect{glm::vec2(0.0f, -0.32f), glm::vec2(0.10f, 0.05f)});
}

void draw_give_up_prompt(overlay_batch& batch, const text_assets& text, const std::string& label) {
    if (label.empty()) {
        return;
    }
    const text_style& style = with_align(find_text_style(text, style_datestamp), text_align::center);
    draw_burned_in(batch, text, style, label, ui_rect{glm::vec2(0.0f, -0.30f), glm::vec2(0.42f, 0.10f)});
}

void draw_address_hints(overlay_batch& batch, const text_assets& text) {
    // Between the datestamp and the swing clock.
    const text_style& style = with_align(find_text_style(text, style_datestamp), text_align::center);
    draw_burned_in(batch, text, style, lookup_text(text, text_hud_address_hints), rect_from_edges(-0.36f, -0.97f, 0.60f, -0.78f));
}

void draw_bag_hints(overlay_batch& batch, const text_assets& text) {
    const text_style& style = with_align(find_text_style(text, style_datestamp), text_align::center);
    draw_burned_in(batch, text, style, lookup_text(text, text_hud_bag_hints), rect_from_edges(-0.36f, -0.97f, 0.60f, -0.78f));
}

void draw_club_picker(overlay_batch& batch, const text_assets& text, const render_club_picker& picker) {
    const text_style& style = find_text_style(text, style_datestamp);
    const ui_rect list = rect_from_edges(0.62f, -0.70f, 0.94f, 0.80f);
    const int count = static_cast<int>(picker.labels.size());
    for (int i = 0; i < count; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const char* row = index == picker.highlighted
            ? (index == picker.in_hand ? text_hud_picker_row_both : text_hud_picker_row_highlighted)
            : (index == picker.in_hand ? text_hud_picker_row_in_hand : text_hud_picker_row);
        draw_burned_in(batch, text, style, format_text(text, row, {{"club", picker.labels[index]}}), rect_row(list, i, count));
    }
    draw_burned_in(batch, text, with_align(style, text_align::center), lookup_text(text, text_hud_picker_hints),
                   rect_from_edges(-0.36f, -0.97f, 0.60f, -0.78f));
}

void draw_vcr_tag(overlay_batch& batch, const text_assets& text, const render_vcr& vcr) {
    if (vcr.tag == vcr_tag::none) {
        return;
    }
    draw_burned_in(batch, text, find_text_style(text, style_vcr_tag),
                   lookup_text(text, vcr.tag == vcr_tag::play ? text_vcr_play : text_vcr_pause), vcr_tag_box());
}
