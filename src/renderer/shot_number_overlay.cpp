#include "renderer/shot_number_overlay.h"

#include "game/text_ids.h"
#include "renderer/hud_overlay.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <array>
#include <optional>

namespace {
// Wide enough for any distance at the tallest box.
constexpr float box_half_width = 0.36f;
}

void draw_shot_number(overlay_batch& batch, const text_assets& text, const render_shot_number& number,
                      const glm::mat4& view_proj) {
    const std::optional<glm::vec2> seen = project_to_screen(view_proj, number.position);
    if (!seen || number.label.empty() || number.alpha <= 0.0f) {
        return;
    }
    const text_style& style = find_text_style(text, number.crit ? style_shot_number_crit : style_shot_number);
    const ui_rect box{*seen + glm::vec2(0.0f, number.half_height + number.lift), glm::vec2(box_half_width, number.half_height)};
    // The text a font pixel short of the box each way, for its edge.
    const float height = static_cast<float>(text.font.height);
    const ui_rect inside{box.center, glm::vec2(box.half_size.x, box.half_size.y * height / (height + 2.0f))};
    const text_layout drawn = layout_text(text.font, style, number.label, inside, batch.grid);
    text_layout layout = drawn;
    const std::array<glm::vec2, 4> edges{glm::vec2(layout.cell.x, 0.0f), glm::vec2(-layout.cell.x, 0.0f),
                                         glm::vec2(0.0f, layout.cell.y), glm::vec2(0.0f, -layout.cell.y)};
    for (const glm::vec2& edge : edges) {
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            layout.lines[i].top_left = drawn.lines[i].top_left + edge;
        }
        layout.truncated = false;
        draw_text_layout(batch, text.font, layout, glm::vec3(0.0f), number.alpha);
    }
    draw_text_layout(batch, text.font, drawn, style.color, number.alpha);
}
