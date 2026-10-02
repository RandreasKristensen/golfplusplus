#include "game/pixel_font_data.h"

#include <nlohmann/json.hpp>

#include "game/utf8.h"

namespace {
using json = nlohmann::json;

std::optional<pixel_glyph> parse_glyph(const json& rows, const int height) {
    if (!rows.is_array() || static_cast<int>(rows.size()) != height) {
        return std::nullopt;
    }

    pixel_glyph glyph;
    for (const json& row : rows) {
        if (!row.is_string()) {
            return std::nullopt;
        }
        const std::string& bits = row.get_ref<const std::string&>();
        if (glyph.pixels.empty()) {
            glyph.width = static_cast<int>(bits.size());
        }
        if (bits.empty() || static_cast<int>(bits.size()) != glyph.width) {
            return std::nullopt;
        }
        for (const char bit : bits) {
            if (bit != '0' && bit != '1') {
                return std::nullopt;
            }
            glyph.pixels.push_back(bit == '1' ? 1U : 0U);
            glyph.lit_pixel_count += bit == '1' ? 1 : 0;
        }
    }
    return glyph;
}

// The key's code point if it is one printable, non-lowercase character.
std::optional<char32_t> font_key(const std::string& key) {
    const std::u32string code_points = decode_utf8(key);
    if (code_points.size() != 1) {
        return std::nullopt;
    }
    const char32_t c = code_points[0];
    const bool control = c < 0x20 || (c >= 0x7F && c < 0xA0);
    if (control || c == 0xFFFD || to_upper_letter(c) != c) {
        return std::nullopt;
    }
    return c;
}
}

std::optional<pixel_font_data> parse_pixel_font(const std::string& text) {
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    pixel_font_data font;
    const auto height = root.find("height");
    if (height == root.end() || !height->is_number_integer() || height->get<int>() <= 0) {
        return std::nullopt;
    }
    font.height = height->get<int>();

    const auto line_gap = root.find("line_gap");
    if (line_gap == root.end() || !line_gap->is_number_integer() || line_gap->get<int>() < 0) {
        return std::nullopt;
    }
    font.line_gap = line_gap->get<int>();

    const auto fallback = root.find("fallback");
    if (fallback == root.end()) {
        return std::nullopt;
    }
    std::optional<pixel_glyph> fallback_glyph = parse_glyph(*fallback, font.height);
    if (!fallback_glyph) {
        return std::nullopt;
    }
    font.fallback = std::move(*fallback_glyph);

    const auto glyphs = root.find("glyphs");
    if (glyphs == root.end() || !glyphs->is_object()) {
        return std::nullopt;
    }
    for (auto it = glyphs->begin(); it != glyphs->end(); ++it) {
        const std::optional<char32_t> key = font_key(it.key());
        if (!key) {
            return std::nullopt;
        }
        std::optional<pixel_glyph> glyph = parse_glyph(it.value(), font.height);
        if (!glyph) {
            return std::nullopt;
        }
        font.glyphs[*key] = std::move(*glyph);
    }

    const auto ellipsis = root.find("ellipsis");
    if (ellipsis == root.end() || !ellipsis->is_string()) {
        return std::nullopt;
    }
    font.ellipsis = decode_utf8(ellipsis->get<std::string>());
    for (const char32_t c : font.ellipsis) {
        if (!font_has_glyph(font, c)) {
            return std::nullopt;
        }
    }
    return font;
}

const pixel_glyph& find_glyph(const pixel_font_data& font, const char32_t value) {
    const auto it = font.glyphs.find(to_upper_letter(value));
    return it == font.glyphs.end() ? font.fallback : it->second;
}

bool font_has_glyph(const pixel_font_data& font, const char32_t value) {
    return font.glyphs.count(to_upper_letter(value)) == 1U;
}

std::string font_charset(const pixel_font_data& font) {
    std::u32string charset;
    for (const auto& entry : font.glyphs) {
        charset.push_back(entry.first);
    }
    return encode_utf8(charset);
}
