#include "game/prop_loader.h"

#include "game/content_files.h"
#include "game/json_util.h"
#include "game/lighting_fields.h"
#include "game/model_kinds.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>

#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>

namespace {
// The objects a variant merges into its base's by name; anything else it
// names replaces the base's.
const std::vector<std::string> merged_keys{"colors", "points", "mounts"};

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

// Its sides one long each and square to each other.
std::optional<prop_mount> parse_mount(const json& object) {
    const std::optional<glm::vec3> at = json_vec3(object, "at");
    const std::optional<glm::vec3> forward = json_vec3(object, "forward");
    const std::optional<glm::vec3> up = json_vec3(object, "up");
    constexpr float tolerance = 0.001f;
    const auto unit = [](const glm::vec3& side) { return std::abs(glm::length(side) - 1.0f) < tolerance; };
    if (!at || !forward || !up || !unit(*forward) || !unit(*up) || std::abs(glm::dot(*forward, *up)) > tolerance) {
        return std::nullopt;
    }
    return prop_mount{*at, *forward, *up};
}

template <typename Map>
bool has_all(const std::vector<std::string>& names, const Map& map) {
    return std::all_of(names.begin(), names.end(), [&map](const std::string& name) { return map.count(name) > 0; });
}

std::optional<prop_model> parse_prop(const json& root) {
    const json* colors = root.is_object() ? json_object(root, "colors") : nullptr;
    const std::optional<std::string> kind_name = root.is_object() ? json_string(root, "kind") : std::nullopt;
    const model_kind* kind = kind_name ? find_model_kind(*kind_name) : nullptr;
    if (colors == nullptr || !colors->is_object() || kind == nullptr) {
        return std::nullopt;
    }
    prop_model model;
    model.kind = *kind_name;
    for (const auto& [name, value] : colors->items()) {
        const std::optional<glm::vec3> color = json_vec3(value);
        if (!color || !is_color(*color)) {
            return std::nullopt;
        }
        model.colors[name] = *color;
    }
    const json* parts = json_array(root, "parts");
    const json* points = json_object(root, "points");
    const json* mounts = json_object(root, "mounts");
    if ((root.contains("parts") && parts == nullptr) || (root.contains("points") && (points == nullptr || !points->is_object())) ||
        (root.contains("mounts") && (mounts == nullptr || !mounts->is_object()))) {
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
    if (mounts != nullptr) {
        for (const auto& [name, value] : mounts->items()) {
            const std::optional<prop_mount> mount = value.is_object() ? parse_mount(value) : std::nullopt;
            if (!mount) {
                return std::nullopt;
            }
            model.mounts[name] = *mount;
        }
    }
    const bool has_tags = std::all_of(kind->part_tags.begin(), kind->part_tags.end(), [&model](const std::string& tag) {
        return std::any_of(model.parts.begin(), model.parts.end(), [&tag](const prop_part& part) { return part.tag == tag; });
    });
    if (!has_all(kind->colors, model.colors) || !has_all(kind->points, model.points) ||
        !has_all(kind->mounts, model.mounts) || !has_tags) {
        return std::nullopt;
    }
    return model;
}
}

std::optional<prop_model> parse_prop_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object() || root->contains("base")) {
        return std::nullopt;
    }
    return parse_prop(*root);
}

prop_catalog_load_result parse_prop_catalog(const std::map<std::string, std::string>& texts) {
    prop_catalog_load_result result;
    std::map<std::string, json> files;
    for (const auto& [name, text] : texts) {
        std::optional<json> root = parse_json(text);
        if (!root) {
            result.error = std::string("cannot parse ") + props_path + "/" + name + ".json";
            return result;
        }
        files[name] = std::move(*root);
    }
    prop_catalog catalog;
    for (const auto& [name, file] : files) {
        const std::optional<json> merged = json_with_base(name, files, merged_keys);
        std::optional<prop_model> model = merged ? parse_prop(*merged) : std::nullopt;
        if (!model) {
            result.error = std::string("cannot load ") + props_path + "/" + name + ".json";
            return result;
        }
        catalog[name] = std::move(*model);
    }
    for (const required_model& required : required_models()) {
        const auto found = catalog.find(required.id);
        if (found == catalog.end() || found->second.kind != required.kind) {
            result.error = std::string("missing ") + props_path + "/" + required.id + ".json";
            return result;
        }
    }
    result.props = std::move(catalog);
    return result;
}

prop_catalog_load_result load_props(const std::string& asset_root) {
    std::map<std::string, std::string> texts;
    for (const std::filesystem::path& path : json_files_in_directory(std::filesystem::path(asset_root) / props_path)) {
        const std::optional<std::string> text = read_text_file(path);
        if (!text) {
            prop_catalog_load_result result;
            result.error = "cannot read " + path.string();
            return result;
        }
        texts[path.stem().string()] = *text;
    }
    return parse_prop_catalog(texts);
}
