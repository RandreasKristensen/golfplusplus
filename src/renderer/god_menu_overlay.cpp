#include "renderer/god_menu_overlay.h"

#include "game/text_ids.h"
#include "renderer/hud_panel.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <algorithm>

#include <glm/vec2.hpp>

namespace {
// The god menu's rows fill its panel; a longer list (the look tuning
// panel's) makes it taller, downwards, at the same row height.
constexpr int panel_rows = 11;
constexpr float panel_top = 0.62f;
constexpr float panel_height = 0.98f;
}

void draw_god_menu(overlay_batch& batch, const text_assets& text, const render_god_menu& menu) {
    const int count = static_cast<int>(menu.rows.size());
    // Shares of the god menu's panel: the title, the rows, the note.
    const float rows_share = 0.66f * static_cast<float>(std::max(count, panel_rows)) / static_cast<float>(panel_rows);
    const float tall = 0.34f + rows_share;
    const ui_rect panel = rect_from_edges(-0.96f, panel_top - panel_height * tall, -0.16f, panel_top);
    draw_hud_panel(batch, panel);
    const ui_rect inner = inset_rect(panel, glm::vec2(0.02f, 0.016f));
    draw_label(batch, text.font, find_text_style(text, style_god_title), menu.title, slice_y(inner, 0.0f, 0.13f / tall));

    const ui_rect rows = slice_y(inner, 0.14f / tall, (0.14f + rows_share) / tall);
    for (int i = 0; i < count; ++i) {
        const render_god_menu_row& row = menu.rows[static_cast<std::size_t>(i)];
        const ui_rect line = inset_rect(rect_row(rows, i, count), glm::vec2(0.0f, 0.006f));
        if (row.selected) {
            draw_overlay_quad(batch, line.center, line.half_size, hud_amber * 0.30f, 0.80f);
        }
        const text_style& label = find_text_style(text, style_god_label);
        const text_style& value = find_text_style(text, row.held ? style_god_held : style_god_live);
        draw_label(batch, text.font, label, row.label, inset_rect(slice_x(line, 0.0f, 0.36f), glm::vec2(0.01f, 0.0f)));
        draw_label(batch, text.font, value, row.value, inset_rect(slice_x(line, 0.36f, 1.0f), glm::vec2(0.01f, 0.0f)));
    }
    draw_label(batch, text.font, find_text_style(text, style_god_note), menu.note, slice_y(inner, (0.16f + rows_share) / tall, 1.0f));
}
