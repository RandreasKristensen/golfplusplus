#include "renderer/god_menu_overlay.h"

#include "game/text_ids.h"
#include "renderer/hud_panel.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <glm/vec2.hpp>

void draw_god_menu(overlay_batch& batch, const text_assets& text, const render_god_menu& menu) {
    const ui_rect panel = rect_from_edges(-0.96f, -0.36f, -0.16f, 0.62f);
    draw_hud_panel(batch, panel);
    const ui_rect inner = inset_rect(panel, glm::vec2(0.02f, 0.016f));
    draw_label(batch, text.font, find_text_style(text, style_god_title), menu.title, slice_y(inner, 0.0f, 0.13f));

    const ui_rect rows = slice_y(inner, 0.14f, 0.80f);
    const int count = static_cast<int>(menu.rows.size());
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
    draw_label(batch, text.font, find_text_style(text, style_god_note), menu.note, slice_y(inner, 0.82f, 1.0f));
}
