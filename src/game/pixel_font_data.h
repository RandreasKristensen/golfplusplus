#pragma once

// Bitmap pixel font as data (assets/fonts/pixel_font.json). Parsed here so the
// charset is available to game code (and later the server's name rules);
// drawn by renderer/pixel_font. No statics: the font is loaded once by app and
// passed around by const reference.

#include <array>
#include <cstdint>
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
    int height = 7;
    // Indexed by ASCII code. Only codes with `defined` set have a glyph.
    std::array<pixel_glyph, 128> glyphs;
    std::array<bool, 128> defined{};
    // Drawn for every character without a glyph, so gaps are obvious.
    pixel_glyph fallback;
};

// Parses the font JSON: { "height": 7, "fallback": [rows], "glyphs": { "A": [rows], ... } }.
// Rows are strings of '0'/'1', all the same width within a glyph. Keys are
// single printable ASCII characters; lowercase letters are rejected (they are
// drawn with the uppercase glyph). Returns nullopt on any malformed entry.
std::optional<pixel_font_data> parse_pixel_font(const std::string& text);

// Lowercase maps to uppercase; anything else without a glyph gives the fallback.
const pixel_glyph& find_glyph(const pixel_font_data& font, char value);
bool font_has_glyph(const pixel_font_data& font, char value);
// Every character with its own glyph (uppercase, digits, space, punctuation),
// in ASCII order. The allowed set for text input fields.
std::string font_charset(const pixel_font_data& font);
