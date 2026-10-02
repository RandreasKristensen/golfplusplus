#pragma once

// Named text styles (assets/ui/text_styles.json): how text fills the box it
// is drawn in, colour and alignment, so drawing code never hand-picks sizes.
// Sizes are relative to the box (renderer/pixel_font.h turns them into a
// whole-number pixel scale). Style names used by code live in game/text_ids.h.

#include <optional>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

enum class text_align { left, center, right };
enum class text_valign { top, center, bottom };

struct text_style {
    // Share of the box height the text aims to fill (all its lines together).
    // Rounded to a whole-number scale, and never larger than the box.
    float fill = 0.0f;
    // Share of the box width the text may use; the rest is side margin.
    float width_fill = 0.0f;
    // Target pixels per font pixel: shrinking to fit stops at min_scale, and
    // max_scale caps text in big boxes (none = only the box limits it).
    int min_scale = 0;
    std::optional<int> max_scale;
    // Break between words when a line is too wide, before shrinking.
    bool wrap = false;
    glm::vec3 color = glm::vec3(0.0f);
    text_align align = text_align::left;
    text_valign valign = text_valign::center;
};

struct text_style_set {
    std::unordered_map<std::string, text_style> styles;
    // Returned for unknown names: loud magenta so a typo is obvious in game.
    text_style missing{0.6f, 0.92f, 1, std::nullopt, false, glm::vec3(1.0f, 0.0f, 1.0f), text_align::left, text_valign::center};
};

// { "styles": { "<name>": { "fill": f, "width_fill": f?, "min_scale": n?, "max_scale": n?,
//   "wrap": b?, "color": [r,g,b], "align": "left"|"center"|"right"?, "valign": "top"|"center"|"bottom"? } } }
std::optional<text_style_set> parse_text_styles(const std::string& text);

const text_style& find_text_style(const text_style_set& set, const char* name);

// Copies with one field replaced, for state colours and per-cell alignment.
text_style with_color(text_style style, glm::vec3 color);
text_style with_align(text_style style, text_align align);
