#include "renderer/text_input.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "game/utf8.h"
#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"

namespace {
constexpr float cursor_blink_period = 1.0f;

// The character to store for `typed`, or nullopt when it is rejected.
std::optional<char32_t> accepted_char(const std::u32string& allowed_chars, const char32_t typed) {
    for (const char32_t candidate : {typed, to_upper_letter(typed)}) {
        if (allowed_chars.find(candidate) != std::u32string::npos) {
            return candidate;
        }
    }
    return std::nullopt;
}
}

text_input_state apply_text_input(const text_input_state& state, const std::string& typed, const bool backspace) {
    text_input_state next = state;
    if (!next.active) {
        return next;
    }

    std::u32string value = decode_utf8(next.value);
    if (backspace && !value.empty()) {
        value.pop_back();
    }
    const std::u32string allowed_chars = decode_utf8(next.allowed_chars);
    for (const char32_t c : decode_utf8(typed)) {
        if (value.size() >= next.max_length) {
            break;
        }
        if (const std::optional<char32_t> accepted = accepted_char(allowed_chars, c)) {
            value.push_back(*accepted);
        }
    }
    next.value = encode_utf8(value);
    return next;
}

void draw_text_input(overlay_batch& batch,
                     const pixel_font_data& font,
                     const text_style& style,
                     const text_input_state& state,
                     const ui_rect& box,
                     const float time_seconds) {
    const glm::vec3 panel_color(0.030f, 0.032f, 0.034f);
    const glm::vec3 outline_color = state.active ? glm::vec3(0.94f, 0.72f, 0.22f) : glm::vec3(0.50f, 0.52f, 0.48f);

    draw_overlay_quad(batch, box.center, box.half_size, panel_color, 0.92f);
    draw_button_outline(batch, box.center, box.half_size, outline_color, 0.94f);

    // Size the text for a full field (plus the cursor) of the widest letter,
    // so it doesn't shrink as it fills up.
    const std::string full_field(state.max_length + 1U, 'W');
    const int fitted = fit_text_scale(font, style, full_field, box, batch.grid);
    const int scale = fitted > 0 ? fitted : style.min_scale;
    const text_layout full = layout_text_at_scale(font, style, full_field, box, batch.grid, scale);
    const text_layout value = layout_text_at_scale(font, style, state.value, box, batch.grid, scale);
    draw_text_layout(batch, font, value, style.color);

    const bool cursor_visible = state.active && std::fmod(std::max(0.0f, time_seconds), cursor_blink_period) < cursor_blink_period * 0.5f;
    if (cursor_visible && !full.lines.empty()) {
        const glm::vec2 line_start = full.lines.front().top_left;
        const float cursor_left = value.lines.empty() ? line_start.x : rect_right(value.bounds) + full.cell.x;
        const glm::vec2 cursor_half(static_cast<float>(find_glyph(font, U'A').width) * full.cell.x * 0.5f,
                                    static_cast<float>(font.height) * full.cell.y * 0.5f);
        draw_overlay_quad(batch,
                          glm::vec2(cursor_left + cursor_half.x, line_start.y - cursor_half.y),
                          cursor_half,
                          style.color,
                          0.90f);
    }
}
