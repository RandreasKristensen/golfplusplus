#include "game/text_style.h"

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

// Defaults for the optional keys.
constexpr float default_width_fill = 0.92f;
constexpr int default_min_scale = 1;

// A fraction in (0, 1], or nullopt when missing or out of range.
std::optional<float> fraction_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number() || it->get<float>() <= 0.0f || it->get<float>() > 1.0f) {
        return std::nullopt;
    }
    return it->get<float>();
}

std::optional<int> scale_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer() || it->get<int>() < 1) {
        return std::nullopt;
    }
    return it->get<int>();
}

std::optional<text_style> parse_style(const json& object) {
    if (!object.is_object()) {
        return std::nullopt;
    }

    text_style style;
    const std::optional<float> fill = fraction_at(object, "fill");
    if (!fill) {
        return std::nullopt;
    }
    style.fill = *fill;

    style.width_fill = default_width_fill;
    if (object.contains("width_fill")) {
        const std::optional<float> width_fill = fraction_at(object, "width_fill");
        if (!width_fill) {
            return std::nullopt;
        }
        style.width_fill = *width_fill;
    }

    style.min_scale = default_min_scale;
    if (object.contains("min_scale")) {
        const std::optional<int> min_scale = scale_at(object, "min_scale");
        if (!min_scale) {
            return std::nullopt;
        }
        style.min_scale = *min_scale;
    }
    if (object.contains("max_scale")) {
        style.max_scale = scale_at(object, "max_scale");
        if (!style.max_scale || *style.max_scale < style.min_scale) {
            return std::nullopt;
        }
    }

    const auto wrap = object.find("wrap");
    if (wrap != object.end()) {
        if (!wrap->is_boolean()) {
            return std::nullopt;
        }
        style.wrap = wrap->get<bool>();
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
        } else if (*align == "right") {
            style.align = text_align::right;
        } else {
            return std::nullopt;
        }
    }

    const auto valign = object.find("valign");
    if (valign != object.end()) {
        if (*valign == "top") {
            style.valign = text_valign::top;
        } else if (*valign == "center") {
            style.valign = text_valign::center;
        } else if (*valign == "bottom") {
            style.valign = text_valign::bottom;
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

text_style with_color(text_style style, const glm::vec3 color) {
    style.color = color;
    return style;
}

text_style with_align(text_style style, const text_align align) {
    style.align = align;
    return style;
}
