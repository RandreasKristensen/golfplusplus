#include "renderer/pixel_font.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/common.hpp>

#include "game/utf8.h"

namespace {
// The box in target pixels, y down from the top of the target.
struct pixel_box {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

bool valid_grid(const overlay_grid& grid) {
    return grid.width > 0 && grid.height > 0;
}

pixel_box to_pixels(const ui_rect& box, const overlay_grid& grid) {
    const glm::vec2 size(static_cast<float>(grid.width), static_cast<float>(grid.height));
    return pixel_box{(rect_left(box) + 1.0f) * 0.5f * size.x,
                     (1.0f - rect_top(box)) * 0.5f * size.y,
                     box.half_size.x * size.x,
                     box.half_size.y * size.y};
}

// Width in font pixels, with one blank pixel between glyphs.
int run_width(const pixel_font_data& font, const std::u32string& glyphs) {
    int width = 0;
    for (const char32_t c : glyphs) {
        width += find_glyph(font, c).width + 1;
    }
    return std::max(0, width - 1);
}

// Height in font pixels of `count` lines.
int block_height(const pixel_font_data& font, const std::size_t count) {
    if (count == 0) {
        return 0;
    }
    return static_cast<int>(count) * font.height + static_cast<int>(count - 1) * font.line_gap;
}

std::vector<std::u32string> split_lines(const std::u32string& text) {
    std::vector<std::u32string> lines(1);
    for (const char32_t c : text) {
        if (c == U'\n') {
            lines.emplace_back();
        } else {
            lines.back().push_back(c);
        }
    }
    return lines;
}

// Greedy word wrap to `max_width` font pixels. Spaces between words that
// share a line are kept as written; a word wider than the line gets a line
// of its own (and is cut off later if it still doesn't fit).
void append_wrapped(const pixel_font_data& font, const std::u32string& line, const int max_width, std::vector<std::u32string>& out) {
    std::u32string current;
    std::size_t i = 0;
    while (i < line.size()) {
        std::size_t word_start = i;
        while (word_start < line.size() && line[word_start] == U' ') {
            ++word_start;
        }
        std::size_t word_end = word_start;
        while (word_end < line.size() && line[word_end] != U' ') {
            ++word_end;
        }
        if (word_start == word_end && !current.empty()) {
            break;  // trailing spaces
        }
        const std::u32string candidate = current + line.substr(i, word_end - i);
        if (current.empty() || run_width(font, candidate) <= max_width) {
            current = candidate;
        } else {
            out.push_back(current);
            current = line.substr(word_start, word_end - word_start);
        }
        i = word_end;
    }
    out.push_back(current);
}

std::vector<std::u32string> break_lines(const pixel_font_data& font,
                                        const text_style& style,
                                        const std::u32string& text,
                                        const int max_width) {
    std::vector<std::u32string> lines;
    for (const std::u32string& line : split_lines(text)) {
        if (style.wrap) {
            append_wrapped(font, line, max_width, lines);
        } else {
            lines.push_back(line);
        }
    }
    return lines;
}

float usable_width(const text_style& style, const pixel_box& box) {
    return box.width * style.width_fill;
}

// Font pixels per line at `scale`.
int width_limit(const text_style& style, const pixel_box& box, const int scale) {
    return static_cast<int>(std::floor(usable_width(style, box) / static_cast<float>(scale)));
}

bool lines_fit(const pixel_font_data& font,
               const text_style& style,
               const std::vector<std::u32string>& lines,
               const pixel_box& box,
               const int scale) {
    const float s = static_cast<float>(scale);
    if (static_cast<float>(block_height(font, lines.size())) * s > box.height) {
        return false;
    }
    for (const std::u32string& line : lines) {
        if (static_cast<float>(run_width(font, line)) * s > usable_width(style, box)) {
            return false;
        }
    }
    return true;
}

// The largest scale the style's fill asks for in this box.
int fill_scale(const pixel_font_data& font, const text_style& style, const std::u32string& text, const pixel_box& box) {
    const float wanted = box.height * style.fill / static_cast<float>(block_height(font, split_lines(text).size()));
    int scale = std::max(style.min_scale, static_cast<int>(std::lround(wanted)));
    if (style.max_scale) {
        scale = std::min(scale, *style.max_scale);
    }
    return scale;
}

// Cuts `line` so it plus the ellipsis is at most `max_width` font pixels.
std::u32string cut_with_ellipsis(const pixel_font_data& font, std::u32string line, const int max_width) {
    while (!line.empty() && run_width(font, line + font.ellipsis) > max_width) {
        line.pop_back();
    }
    while (!line.empty() && line.back() == U' ') {
        line.pop_back();
    }
    line += font.ellipsis;
    while (!line.empty() && run_width(font, line) > max_width) {
        line.pop_back();
    }
    return line;
}

float align_offset(const float free_space, const float alignment) {
    return std::max(0.0f, free_space) * alignment;
}

float horizontal_alignment(const text_align align) {
    switch (align) {
    case text_align::left:
        return 0.0f;
    case text_align::center:
        return 0.5f;
    case text_align::right:
        return 1.0f;
    }
    return 0.0f;
}

float vertical_alignment(const text_valign valign) {
    switch (valign) {
    case text_valign::top:
        return 0.0f;
    case text_valign::center:
        return 0.5f;
    case text_valign::bottom:
        return 1.0f;
    }
    return 0.5f;
}
}

int fit_text_scale(const pixel_font_data& font,
                   const text_style& style,
                   const std::string& text,
                   const ui_rect& box,
                   const overlay_grid& grid) {
    if (!valid_grid(grid)) {
        return 0;
    }
    const std::u32string glyphs = decode_utf8(text);
    const pixel_box pixels = to_pixels(box, grid);
    for (int scale = fill_scale(font, style, glyphs, pixels); scale >= style.min_scale; --scale) {
        if (lines_fit(font, style, break_lines(font, style, glyphs, width_limit(style, pixels, scale)), pixels, scale)) {
            return scale;
        }
    }
    return 0;
}

text_layout layout_text_at_scale(const pixel_font_data& font,
                                 const text_style& style,
                                 const std::string& text,
                                 const ui_rect& box,
                                 const overlay_grid& grid,
                                 const int scale) {
    text_layout layout;
    layout.bounds = ui_rect{box.center, glm::vec2(0.0f)};
    if (!valid_grid(grid) || scale < 1 || text.empty()) {
        return layout;
    }
    layout.scale = scale;
    layout.cell = glm::vec2(2.0f * static_cast<float>(scale) / static_cast<float>(grid.width),
                            2.0f * static_cast<float>(scale) / static_cast<float>(grid.height));

    const pixel_box pixels = to_pixels(box, grid);
    const float s = static_cast<float>(scale);
    const int max_width = width_limit(style, pixels, scale);
    std::vector<std::u32string> lines = break_lines(font, style, decode_utf8(text), max_width);

    std::size_t kept = lines.size();
    while (kept > 0 && static_cast<float>(block_height(font, kept)) * s > pixels.height) {
        --kept;
    }
    if (kept < lines.size()) {
        layout.truncated = true;
        lines.resize(kept);
        if (!lines.empty()) {
            lines.back() = cut_with_ellipsis(font, lines.back(), max_width);
        }
    }
    for (std::u32string& line : lines) {
        if (run_width(font, line) > max_width) {
            layout.truncated = true;
            line = cut_with_ellipsis(font, line, max_width);
        }
    }
    if (lines.empty()) {
        return layout;
    }

    const float margin = (pixels.width - usable_width(style, pixels)) * 0.5f;
    const float block = static_cast<float>(block_height(font, lines.size())) * s;
    const float top = std::round(pixels.top + align_offset(pixels.height - block, vertical_alignment(style.valign)));
    const float line_step = static_cast<float>(font.height + font.line_gap) * s;
    const glm::vec2 grid_size(static_cast<float>(grid.width), static_cast<float>(grid.height));

    glm::vec2 min_corner(1.0f);
    glm::vec2 max_corner(-1.0f);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float width = static_cast<float>(run_width(font, lines[i])) * s;
        const float left = std::round(pixels.left + margin +
                                      align_offset(usable_width(style, pixels) - width, horizontal_alignment(style.align)));
        const float line_top = top + static_cast<float>(i) * line_step;
        const glm::vec2 top_left(left * 2.0f / grid_size.x - 1.0f, 1.0f - line_top * 2.0f / grid_size.y);
        const glm::vec2 bottom_right((left + width) * 2.0f / grid_size.x - 1.0f,
                                     1.0f - (line_top + static_cast<float>(font.height) * s) * 2.0f / grid_size.y);
        min_corner = glm::min(min_corner, glm::vec2(top_left.x, bottom_right.y));
        max_corner = glm::max(max_corner, glm::vec2(bottom_right.x, top_left.y));
        layout.lines.push_back(text_line{std::move(lines[i]), top_left});
    }
    layout.bounds = ui_rect{(min_corner + max_corner) * 0.5f, glm::max(max_corner - min_corner, glm::vec2(0.0f)) * 0.5f};
    return layout;
}

text_layout layout_text(const pixel_font_data& font,
                        const text_style& style,
                        const std::string& text,
                        const ui_rect& box,
                        const overlay_grid& grid) {
    const int scale = fit_text_scale(font, style, text, box, grid);
    return layout_text_at_scale(font, style, text, box, grid, scale > 0 ? scale : style.min_scale);
}

void draw_text_layout(overlay_batch& batch, const pixel_font_data& font, const text_layout& layout, const glm::vec3 color) {
    if (layout.truncated) {
        ++batch.truncated_text_count;
    }
    const glm::vec2 half_cell = layout.cell * 0.5f;
    for (const text_line& line : layout.lines) {
        glm::vec2 cursor = line.top_left;
        for (const char32_t c : line.glyphs) {
            const pixel_glyph& glyph = find_glyph(font, c);
            for (int y = 0; y < font.height; ++y) {
                for (int x = 0; x < glyph.width; ++x) {
                    if (glyph.pixels[static_cast<std::size_t>(y * glyph.width + x)] == 0U) {
                        continue;
                    }
                    draw_overlay_quad(batch,
                                      cursor + glm::vec2((static_cast<float>(x) + 0.5f) * layout.cell.x,
                                                         -(static_cast<float>(y) + 0.5f) * layout.cell.y),
                                      half_cell,
                                      color);
                }
            }
            cursor.x += static_cast<float>(glyph.width + 1) * layout.cell.x;
        }
    }
}

void draw_label(overlay_batch& batch,
                const pixel_font_data& font,
                const text_style& style,
                const std::string& text,
                const ui_rect& box) {
    draw_text_layout(batch, font, layout_text(font, style, text, box, batch.grid), style.color);
}
