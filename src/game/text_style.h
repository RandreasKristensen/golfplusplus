#pragma once

// Named text styles (assets/ui/text_styles.json): pixel size, fit limit,
// colour and alignment, so drawing code never hand-picks them. Style names
// used by code live in game/text_ids.h.

#include <optional>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

enum class text_align {
    left,   // anchor is the text's top-left corner
    center  // anchor is the text's centre
};

struct text_style {
    // Size of one font pixel in overlay clip units, and the upper bound when
    // the text is shrunk to fit a box.
    float pixel_size = 0.01f;
    // Lower bound when shrinking to fit. Defaults to pixel_size.
    float min_pixel_size = 0.01f;
    glm::vec3 color = glm::vec3(1.0f);
    text_align align = text_align::left;
};

struct text_style_set {
    std::unordered_map<std::string, text_style> styles;
    // Returned for unknown names: loud magenta so a typo is obvious in game.
    text_style missing{0.012f, 0.008f, glm::vec3(1.0f, 0.0f, 1.0f), text_align::left};
};

// { "styles": { "<name>": { "pixel_size": f, "min_pixel_size": f?, "color": [r,g,b], "align": "left"|"center"? } } }
std::optional<text_style_set> parse_text_styles(const std::string& text);

const text_style& find_text_style(const text_style_set& set, const char* name);

// Copies with one field replaced, for layout-computed sizes and state colours.
text_style with_pixel_size(text_style style, float pixel_size);
text_style with_color(text_style style, glm::vec3 color);
text_style with_align(text_style style, text_align align);
