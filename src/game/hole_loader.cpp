#include "game/hole_loader.h"

#include "game/json_util.h"

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace {
// Trees smaller than this are treated as authoring mistakes.
constexpr float min_tree_dimension = 0.01f;
// A zone narrower than this (radius or half-size, metres) is a broken one.
constexpr float min_zone_radius = 0.01f;

material_zone_type material_type_from_string(const std::string& value) {
    if (value == "green") {
        return material_zone_type::green;
    }
    if (value == "bunker") {
        return material_zone_type::bunker;
    }
    if (value == "water") {
        return material_zone_type::water;
    }
    return material_zone_type::unknown;
}

// One zone, an ellipse (`center`, `radii`, `rotation_degrees`). nullopt
// when any of those is missing or a radius is not positive, and for the
// `radius` and `bounds` shapes zones no longer have.
std::optional<material_zone> material_zone_from_json(const json& object) {
    if (!object.is_object() || object.contains("radius") || object.contains("bounds")) {
        return std::nullopt;
    }
    const std::optional<glm::vec3> center = json_vec3(object, "center");
    const std::optional<glm::vec2> radii = json_vec2(object, "radii");
    const std::optional<float> rotation_degrees = json_float(object, "rotation_degrees");
    if (!center || !radii || !rotation_degrees || !std::isfinite(*rotation_degrees)) {
        return std::nullopt;
    }
    material_zone zone;
    zone.type = material_type_from_string(json_string(object, "type").value_or(""));
    zone.center = *center;
    zone.radii = *radii;
    zone.rotation = glm::radians(*rotation_degrees);
    if (!(zone.radii.x >= min_zone_radius) || !(zone.radii.y >= min_zone_radius) ||
        !std::isfinite(zone.radii.x) || !std::isfinite(zone.radii.y)) {
        return std::nullopt;
    }
    return zone;
}

// Every zone, or nullopt when any of them is malformed.
std::optional<std::vector<material_zone>> material_zones_from_json(const json& root) {
    std::vector<material_zone> zones;
    const json* array = json_array(root, "material_zones");
    if (array == nullptr) {
        return zones;
    }
    for (const json& object : *array) {
        const std::optional<material_zone> zone = material_zone_from_json(object);
        if (!zone) {
            return std::nullopt;
        }
        zones.push_back(*zone);
    }
    return zones;
}

float tree_dimension(const json& object, const char* key, const float fallback) {
    return std::max(min_tree_dimension, json_float(object, key).value_or(fallback));
}

std::vector<tree_instance> trees_from_json(const json& root) {
    std::vector<tree_instance> trees;
    const json* array = json_array(root, "trees");
    if (array == nullptr) {
        return trees;
    }

    for (const json& object : *array) {
        const std::optional<glm::vec3> position = json_vec3(object, "position");
        if (!position) {
            continue;
        }
        tree_instance tree;
        tree.position = *position;
        tree.shape.trunk_radius = tree_dimension(object, "trunk_radius", default_tree_shape.trunk_radius);
        tree.shape.trunk_height = tree_dimension(object, "trunk_height", default_tree_shape.trunk_height);
        tree.shape.leaf_radius = tree_dimension(object, "leaf_radius", default_tree_shape.leaf_radius);
        tree.shape.leaf_height = tree_dimension(object, "leaf_height", default_tree_shape.leaf_height);
        trees.push_back(tree);
    }
    return trees;
}
}

std::optional<hole_data> parse_hole_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }

    const std::optional<glm::vec3> tee = json_vec3(*root, "tee");
    const std::optional<glm::vec3> pin = json_vec3(*root, "pin");
    const json* spline = json_object(*root, "spline");
    if (!tee || !pin || spline == nullptr) {
        return std::nullopt;
    }

    const std::optional<float> width = json_float(*spline, "width");
    const json* control_points_json = json_array(*spline, "control_points");
    const std::optional<std::vector<glm::vec3>> control_points =
        control_points_json != nullptr ? json_vec3_array(*control_points_json) : std::nullopt;
    if (!width || *width <= 0.0f || !control_points || control_points->size() < 2) {
        return std::nullopt;
    }
    // Optional; when given, one number per control point.
    std::vector<float> bank;
    if (const json* bank_json = json_array(*spline, "bank")) {
        if (bank_json->size() != control_points->size()) {
            return std::nullopt;
        }
        for (const json& value : *bank_json) {
            if (!value.is_number()) {
                return std::nullopt;
            }
            bank.push_back(value.get<float>());
        }
    }

    hole_data hole;
    hole.id = json_string(*root, "id").value_or("");
    hole.name = json_string(*root, "name").value_or(hole.id);
    hole.par = json_int(*root, "par").value_or(default_hole_par);
    hole.wind_seed = json_uint32(*root, "wind_seed").value_or(default_wind_seed);
    hole.tee_position = *tee;
    hole.pin_position = *pin;
    hole.spline.width = *width;
    hole.spline.rough_width = std::max(*width, json_float(*spline, "rough_width").value_or(*width));
    hole.spline.control_points = *control_points;
    hole.spline.bank = std::move(bank);
    std::optional<std::vector<material_zone>> zones = material_zones_from_json(*root);
    if (!zones) {
        return std::nullopt;
    }
    hole.material_zones = std::move(*zones);
    hole.trees = trees_from_json(*root);
    return hole;
}

