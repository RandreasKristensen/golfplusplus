#pragma once

// Font + string table + text styles, loaded once by app and passed by const
// reference to render-data assembly and the renderer. Never global.

#include <optional>
#include <string>

#include "game/pixel_font_data.h"
#include "game/string_table.h"
#include "game/text_style.h"

struct text_assets {
    pixel_font_data font;
    string_table strings;
    text_style_set styles;
};

// Paths relative to the asset root.
constexpr const char* text_font_path = "fonts/pixel_font.json";
constexpr const char* text_strings_path = "text/en.json";
constexpr const char* text_styles_path = "ui/text_styles.json";

std::optional<text_assets> parse_text_assets(const std::string& font_json,
                                             const std::string& strings_json,
                                             const std::string& styles_json);
std::optional<text_assets> load_text_assets(const std::string& asset_root);

// Shorthands over the bundled parts.
std::string lookup_text(const text_assets& text, const char* key);
std::string format_text(const text_assets& text, const char* key, std::initializer_list<text_arg> args);
const text_style& find_text_style(const text_assets& text, const char* name);
