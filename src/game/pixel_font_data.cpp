#include "game/pixel_font_data.h"

#include <cctype>

#include <nlohmann/json.hpp>

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

char to_upper_ascii(const char value) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

bool is_font_key(const std::string& key) {
    if (key.size() != 1) {
        return false;
    }
    const unsigned char c = static_cast<unsigned char>(key[0]);
    return c >= 0x20 && c < 0x7f && !std::islower(c);
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
        if (!is_font_key(it.key())) {
            return std::nullopt;
        }
        std::optional<pixel_glyph> glyph = parse_glyph(it.value(), font.height);
        if (!glyph) {
            return std::nullopt;
        }
        const std::size_t index = static_cast<unsigned char>(it.key()[0]);
        font.glyphs[index] = std::move(*glyph);
        font.defined[index] = true;
    }
    return font;
}

const pixel_glyph& find_glyph(const pixel_font_data& font, const char value) {
    const unsigned char code = static_cast<unsigned char>(to_upper_ascii(value));
    if (code < font.glyphs.size() && font.defined[code]) {
        return font.glyphs[code];
    }
    return font.fallback;
}

bool font_has_glyph(const pixel_font_data& font, const char value) {
    const unsigned char code = static_cast<unsigned char>(to_upper_ascii(value));
    return code < font.defined.size() && font.defined[code];
}

std::string font_charset(const pixel_font_data& font) {
    std::string charset;
    for (std::size_t code = 0; code < font.defined.size(); ++code) {
        if (font.defined[code]) {
            charset.push_back(static_cast<char>(code));
        }
    }
    return charset;
}
