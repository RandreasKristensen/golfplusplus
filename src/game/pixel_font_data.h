#pragma once

// Bitmap pixel font as data (assets/fonts/pixel_font.json). Parsed here so the
// charset is available to game code (and later the server's name rules);
// drawn by renderer/pixel_font. No statics: the font is loaded once by app and
// passed around by const reference.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct pixel_glyph {
    int width = 0;
    // Row-major, `height * width` entries, 1 = lit.
    std::vector<std::uint8_t> pixels;
    int lit_pixel_count = 0;
};

struct pixel_font_data {
    int height = 0;
    // Blank font pixels between lines.
    int line_gap = 0;
    // Ends a line that had to be cut off to fit its box (code points).
    std::u32string ellipsis;
    // Keyed by Unicode code point; uppercase only.
    std::map<char32_t, pixel_glyph> glyphs;
    // Drawn for every character without a glyph, so gaps are obvious.
    pixel_glyph fallback;
};

// Parses the font JSON: { "height": 7, "line_gap": 2, "ellipsis": "...",
// "fallback": [rows], "glyphs": { "A": [rows], ... } }.
// Rows are strings of '0'/'1', all the same width within a glyph. Keys are
// single printable UTF-8 characters; lowercase letters are rejected (they are
// drawn with the uppercase glyph, see to_upper_letter in game/utf8.h).
// Every ellipsis character needs a glyph. Returns nullopt on any malformed
// entry.
std::optional<pixel_font_data> parse_pixel_font(const std::string& text);

// Lowercase maps to uppercase; anything else without a glyph gives the fallback.
const pixel_glyph& find_glyph(const pixel_font_data& font, char32_t value);
bool font_has_glyph(const pixel_font_data& font, char32_t value);
// Every character with its own glyph (uppercase, digits, space, punctuation),
// UTF-8 encoded in code point order. The allowed set for text input fields.
std::string font_charset(const pixel_font_data& font);
