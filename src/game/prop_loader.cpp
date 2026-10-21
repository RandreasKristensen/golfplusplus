#include "game/prop_loader.h"

#include "game/content_files.h"
#include "game/json_util.h"
#include "game/lighting_fields.h"

#include <algorithm>
#include <filesystem>
#include <iterator>

#include <glm/vector_relational.hpp>

namespace {
bool is_color(const glm::vec3& color) {
    return !glm::any(glm::lessThan(color, glm::vec3(0.0f))) && !glm::any(glm::greaterThan(color, glm::vec3(1.0f)));
}

bool is_glow_floor(const std::string& name) {
    return std::any_of(std::begin(glow_fields), std::end(glow_fields),
                       [&name](const share_field<glow_floors>& field) { return name == field.key; });
}

std::optional<prop_shape> parse_shape(const std::string& name) {
    if (name == "cylinder") {
        return prop_shape::cylinder;
    }
    if (name == "sphere") {
        return prop_shape::sphere;
    }
    if (name == "box") {
        return prop_shape::box;
    }
    if (name == "panel") {
        return prop_shape::panel;
    }
    return std::nullopt;
}

std::optional<prop_part> parse_part(const json& object, const std::map<std::string, glm::vec3>& colors) {
    const std::optional<std::string> shape_name = json_string(object, "shape");
    const std::optional<prop_shape> shape = shape_name ? parse_shape(*shape_name) : std::nullopt;
    const std::optional<std::string> color = json_string(object, "color");
    if (!shape || !color || colors.count(*color) == 0) {
        return std::nullopt;
    }
    prop_part part;
    part.shape = *shape;
    part.color = colors.at(*color);
    part.glow = json_string(object, "glow").value_or("");
    part.tag = json_string(object, "tag").value_or("");
    part.turn_degrees = json_vec3(object, "turn").value_or(glm::vec3(0.0f));
    if (!part.glow.empty() && !is_glow_floor(part.glow)) {
        return std::nullopt;
    }

    switch (part.shape) {
    case prop_shape::cylinder: {
        const std::optional<glm::vec3> from = json_vec3(object, "from");
        const std::optional<glm::vec3> to = json_vec3(object, "to");
        const std::optional<float> radius = json_float(object, "radius");
        if (!from || !to || !radius || !(*radius > 0.0f) || *from == *to) {
            return std::nullopt;
        }
        part.from = *from;
        part.to = *to;
        part.radius = *radius;
        return part;
    }
    case prop_shape::sphere: {
        const std::optional<glm::vec3> at = json_vec3(object, "at");
        const std::optional<float> radius = json_float(object, "radius");
        if (!at || !radius || !(*radius > 0.0f)) {
            return std::nullopt;
        }
        part.at = *at;
        part.radius = *radius;
        return part;
    }
    case prop_shape::box: {
        const std::optional<glm::vec3> at = json_vec3(object, "at");
        const std::optional<glm::vec3> size = json_vec3(object, "size");
        if (!at || !size || glm::any(glm::lessThanEqual(*size, glm::vec3(0.0f)))) {
            return std::nullopt;
        }
        part.at = *at;
        part.size = *size;
        return part;
    }
    case prop_shape::panel: {
        const std::optional<glm::vec3> at = json_vec3(object, "at");
        const std::optional<glm::vec2> size = json_vec2(object, "size");
        if (!at || !size || !(size->x > 0.0f) || !(size->y > 0.0f)) {
            return std::nullopt;
        }
        part.at = *at;
        part.size = glm::vec3(*size, 0.0f);
        return part;
    }
    }
    return std::nullopt;
}

std::optional<prop_model> load_prop(const std::string& asset_root,
                                    const char* name,
                                    const std::vector<std::string>& needed_colors,
                                    const std::vector<std::string>& needed_points) {
    const std::optional<std::string> text =
        read_text_file(std::filesystem::path(asset_root) / props_path / (std::string(name) + ".json"));
    return text ? parse_prop_from_text(*text, needed_colors, needed_points) : std::nullopt;
}
}

std::optional<prop_model> parse_prop_from_text(const std::string& text,
                                               const std::vector<std::string>& needed_colors,
                                               const std::vector<std::string>& needed_points) {
    const std::optional<json> root = parse_json(text);
    const json* colors = root && root->is_object() ? json_object(*root, "colors") : nullptr;
    if (colors == nullptr || !colors->is_object()) {
        return std::nullopt;
    }
    prop_model model;
    for (const auto& [name, value] : colors->items()) {
        const std::optional<glm::vec3> color = json_vec3(value);
        if (!color || !is_color(*color)) {
            return std::nullopt;
        }
        model.colors[name] = *color;
    }
    const json* parts = json_array(*root, "parts");
    const json* points = json_object(*root, "points");
    if ((root->contains("parts") && parts == nullptr) || (root->contains("points") && points == nullptr)) {
        return std::nullopt;
    }
    if (parts != nullptr) {
        for (const json& object : *parts) {
            std::optional<prop_part> part = object.is_object() ? parse_part(object, model.colors) : std::nullopt;
            if (!part) {
                return std::nullopt;
            }
            model.parts.push_back(std::move(*part));
        }
    }
    if (points != nullptr) {
        for (const auto& [name, value] : points->items()) {
            const std::optional<glm::vec3> point = json_vec3(value);
            if (!point) {
                return std::nullopt;
            }
            model.points[name] = *point;
        }
    }
    const bool has_colors = std::all_of(needed_colors.begin(), needed_colors.end(),
                                        [&model](const std::string& name) { return model.colors.count(name) > 0; });
    const bool has_points = std::all_of(needed_points.begin(), needed_points.end(),
                                        [&model](const std::string& name) { return model.points.count(name) > 0; });
    if (!has_colors || !has_points) {
        return std::nullopt;
    }
    return model;
}

prop_set_load_result load_props(const std::string& asset_root) {
    prop_set_load_result result;
    prop_set props;
    const auto load = [&](prop_model& out, const char* name, const std::vector<std::string>& colors,
                          const std::vector<std::string>& points) {
        std::optional<prop_model> model = load_prop(asset_root, name, colors, points);
        if (!model) {
            if (result.error.empty()) {
                result.error = std::string("cannot load ") + props_path + "/" + name + ".json";
            }
            return;
        }
        out = std::move(*model);
    };
    load(props.cigarette, "cigarette", {"ember_dim", "ember_bright", "smoke"}, {"smoke"});
    load(props.beer_can, "beer_can", {"beer", "foam"}, {"pour"});
    load(props.golf_cart, "golf_cart", {"trim", "rubber", "hub_cap"}, {});
    load(props.tree, "tree", {"trunk", "leaves"}, {});
    load(props.fence, "fence", {"post"}, {});
    load(props.floodlight, "floodlight", {"pole", "head"}, {});
    load(props.hole_sign, "hole_sign", {"board", "post"}, {});
    if (result.error.empty()) {
        result.props = std::move(props);
    }
    return result;
}
