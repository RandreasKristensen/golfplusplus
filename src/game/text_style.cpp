#include "game/text_style.h"

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

std::optional<float> positive_float_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number() || it->get<float>() <= 0.0f) {
        return std::nullopt;
    }
    return it->get<float>();
}

std::optional<text_style> parse_style(const json& object) {
    if (!object.is_object()) {
        return std::nullopt;
    }

    text_style style;
    const std::optional<float> pixel_size = positive_float_at(object, "pixel_size");
    if (!pixel_size) {
        return std::nullopt;
    }
    style.pixel_size = *pixel_size;
    style.min_pixel_size = *pixel_size;
    if (object.contains("min_pixel_size")) {
        const std::optional<float> min_pixel_size = positive_float_at(object, "min_pixel_size");
        if (!min_pixel_size || *min_pixel_size > style.pixel_size) {
            return std::nullopt;
        }
        style.min_pixel_size = *min_pixel_size;
    }

    const auto color = object.find("color");
    if (color == object.end() || !color->is_array() || color->size() != 3) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(*color)[i].is_number()) {
            return std::nullopt;
        }
        style.color[static_cast<glm::length_t>(i)] = (*color)[i].get<float>();
    }

    const auto align = object.find("align");
    if (align != object.end()) {
        if (*align == "left") {
            style.align = text_align::left;
        } else if (*align == "center") {
            style.align = text_align::center;
        } else {
            return std::nullopt;
        }
    }
    return style;
}
}

std::optional<text_style_set> parse_text_styles(const std::string& text) {
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }
    const auto styles = root.find("styles");
    if (styles == root.end() || !styles->is_object()) {
        return std::nullopt;
    }

    text_style_set set;
    for (auto it = styles->begin(); it != styles->end(); ++it) {
        const std::optional<text_style> style = parse_style(it.value());
        if (!style) {
            return std::nullopt;
        }
        set.styles.emplace(it.key(), *style);
    }
    return set;
}

const text_style& find_text_style(const text_style_set& set, const char* name) {
    const auto it = set.styles.find(name);
    return it == set.styles.end() ? set.missing : it->second;
}

text_style with_pixel_size(text_style style, const float pixel_size) {
    style.pixel_size = pixel_size;
    return style;
}

text_style with_color(text_style style, const glm::vec3 color) {
    style.color = color;
    return style;
}

text_style with_align(text_style style, const text_align align) {
    style.align = align;
    return style;
}
