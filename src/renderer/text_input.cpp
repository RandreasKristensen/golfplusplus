#include "renderer/text_input.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"

namespace {
constexpr float cursor_blink_period = 1.0f;

bool is_allowed(const std::string& allowed_chars, const char value) {
    return allowed_chars.find(value) != std::string::npos;
}

// The character to store for `typed`, or '\0' when it is rejected.
char accepted_char(const std::string& allowed_chars, const char typed) {
    if (is_allowed(allowed_chars, typed)) {
        return typed;
    }
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(typed)));
    if (upper != typed && is_allowed(allowed_chars, upper)) {
        return upper;
    }
    return '\0';
}
}

text_input_state apply_text_input(const text_input_state& state, const std::string& typed, const bool backspace) {
    text_input_state next = state;
    if (!next.active) {
        return next;
    }

    if (backspace && !next.value.empty()) {
        next.value.pop_back();
    }
    for (const char c : typed) {
        if (next.value.size() >= next.max_length) {
            break;
        }
        const char accepted = accepted_char(next.allowed_chars, c);
        if (accepted != '\0') {
            next.value.push_back(accepted);
        }
    }
    return next;
}

void draw_text_input(overlay_batch& batch,
                     const pixel_font_data& font,
                     const text_style& style,
                     const text_input_state& state,
                     const glm::vec2 center,
                     const glm::vec2 half_size,
                     const float time_seconds) {
    const glm::vec3 panel_color(0.030f, 0.032f, 0.034f);
    const glm::vec3 outline_color = state.active ? glm::vec3(0.94f, 0.72f, 0.22f) : glm::vec3(0.50f, 0.52f, 0.48f);

    draw_overlay_quad(batch, center, half_size, panel_color, 0.92f);
    draw_button_outline(batch, center, half_size, outline_color, 0.94f);

    // Size the text for a full field so it doesn't shrink as it fills up.
    const float glyph_width = static_cast<float>(find_glyph(font, 'W').width + 1);
    const float full_width = std::max(1.0f, glyph_width * static_cast<float>(state.max_length + 1U));
    const float width_fit = half_size.x * 2.0f * 0.88f / full_width;
    const float height_fit = half_size.y * 2.0f * 0.70f / static_cast<float>(font.height);
    const float pixel_size = std::clamp(std::min(width_fit, height_fit), style.min_pixel_size, style.pixel_size);
    const text_style text = with_align(with_pixel_size(style, pixel_size), text_align::left);

    const float text_height = static_cast<float>(font.height) * pixel_size;
    const glm::vec2 text_top_left(center.x - half_size.x + pixel_size * 3.0f, center.y + text_height * 0.5f);
    draw_text(batch, font, text, state.value, text_top_left);

    const bool cursor_visible = state.active && std::fmod(std::max(0.0f, time_seconds), cursor_blink_period) < cursor_blink_period * 0.5f;
    if (cursor_visible) {
        const float gap = state.value.empty() ? 0.0f : pixel_size;
        const float cursor_x = text_top_left.x + pixel_text_width(font, state.value, pixel_size) + gap;
        const float cursor_width = static_cast<float>(find_glyph(font, 'A').width) * pixel_size;
        draw_overlay_quad(batch,
                          glm::vec2(cursor_x + cursor_width * 0.5f, center.y),
                          glm::vec2(cursor_width * 0.5f, text_height * 0.5f),
                          style.color,
                          0.90f);
    }
}
