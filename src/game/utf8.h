#pragma once

// UTF-8 <-> code points for on-screen text. Strings in assets/text and typed
// text (SDL_TEXTINPUT) are UTF-8; the pixel font is keyed by code point.

#include <string>

// Malformed or truncated sequences decode to U+FFFD, which has no glyph and
// so draws as the fallback box.
std::u32string decode_utf8(const std::string& text);
std::string encode_utf8(const std::u32string& code_points);

// Uppercase form of ASCII and Latin-1 letters (a-z, à-þ except ÷); every
// other code point is returned unchanged. The font has no lowercase glyphs.
char32_t to_upper_letter(char32_t code_point);
