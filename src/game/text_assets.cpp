#include "game/text_assets.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
}

std::optional<text_assets> parse_text_assets(const std::string& font_json,
                                             const std::string& strings_json,
                                             const std::string& styles_json) {
    std::optional<pixel_font_data> font = parse_pixel_font(font_json);
    std::optional<string_table> strings = parse_string_table(strings_json);
    std::optional<text_style_set> styles = parse_text_styles(styles_json);
    if (!font || !strings || !styles) {
        return std::nullopt;
    }
    return text_assets{std::move(*font), std::move(*strings), std::move(*styles)};
}

std::optional<text_assets> load_text_assets(const std::string& asset_root) {
    const std::filesystem::path root(asset_root);
    const std::optional<std::string> font = read_file(root / text_font_path);
    const std::optional<std::string> strings = read_file(root / text_strings_path);
    const std::optional<std::string> styles = read_file(root / text_styles_path);
    if (!font || !strings || !styles) {
        return std::nullopt;
    }
    return parse_text_assets(*font, *strings, *styles);
}

std::string lookup_text(const text_assets& text, const char* key) {
    return lookup_text(text.strings, key);
}

std::string format_text(const text_assets& text, const char* key, const std::initializer_list<text_arg> args) {
    return format_text(text.strings, key, args);
}

const text_style& find_text_style(const text_assets& text, const char* name) {
    return find_text_style(text.styles, name);
}
