#include "game/hole_loader.h"

#include "physics/material_zone.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace {
// Trees smaller than this are authoring mistakes.
constexpr float min_tree_dimension = 0.01f;
// A zone smaller than this (square metres) is a broken one.
constexpr float min_zone_area = 0.01f;
// How far (metres) a zone's drawn edge may stray from its curve.
constexpr float zone_curve_tolerance = 0.05f;

std::optional<material_zone_type> material_type_from_string(const std::string& value) {
    if (value == "green") {
        return material_zone_type::green;
    }
    if (value == "bunker") {
        return material_zone_type::bunker;
    }
    if (value == "water") {
        return material_zone_type::water;
    }
    if (value == "fairway") {
        return material_zone_type::fairway;
    }
    if (value == "heavy_rough") {
        return material_zone_type::heavy_rough;
    }
    return std::nullopt;
}

// One zone: its `type`, its `outline` ([[x, z], ...] mapped round its edge,
// its edge the curve through them) and, on a green, its own `green_stimp_feet`.
parsed<material_zone> material_zone_from_json(const json& object) {
    const std::optional<material_zone_type> type = material_type_from_string(json_string(object, "type").value_or(""));
    if (!type) {
        return {std::nullopt, "type: not green, bunker, water, fairway or heavy_rough"};
    }
    const json* points = json_array(object, "outline");
    if (points == nullptr || points->size() < 3U) {
        return {std::nullopt, "outline: fewer than three points"};
    }
    std::vector<glm::vec2> outline;
    for (std::size_t i = 0; i < points->size(); ++i) {
        const std::optional<glm::vec2> xz = json_vec2((*points)[i]);
        if (!xz) {
            return {std::nullopt, "outline[" + std::to_string(i) + "]: not [x, z]"};
        }
        outline.push_back(*xz);
    }
    material_zone zone = make_material_zone(*type, rounded_outline(outline, zone_curve_tolerance));
    if (!(zone_area(zone) >= min_zone_area)) {
        return {std::nullopt, "outline: no area"};
    }
    if (object.contains("green_stimp_feet")) {
        const auto speed = json_float(object, "green_stimp_feet");
        if (!speed || *speed <= 0 || zone.type != material_zone_type::green) {
            return {std::nullopt, "green_stimp_feet: not a speed above 0 on a green"};
        }
        zone.green_stimp_feet = *speed;
    }
    return {zone, ""};
}

const std::pair<const char*, float tree_shape::*> tree_dimensions[] = {
    {"trunk_radius", &tree_shape::trunk_radius},
    {"trunk_height", &tree_shape::trunk_height},
    {"leaf_radius", &tree_shape::leaf_radius},
    {"leaf_height", &tree_shape::leaf_height},
};

parsed<tree_instance> tree_from_json(const json& object) {
    const std::optional<glm::vec3> position = json_vec3(object, "position");
    if (!position) {
        return {std::nullopt, "position: not [x, y, z]"};
    }
    tree_instance tree;
    tree.position = *position;
    if (object.contains("species")) {
        const std::optional<std::string> species = json_string(object, "species");
        if (!species || species->empty()) {
            return {std::nullopt, "species: not a species name"};
        }
        tree.species = *species;
    }
    for (const auto& [key, dimension] : tree_dimensions) {
        const std::optional<float> size = json_float_or(object, key, default_tree_shape.*dimension);
        if (!size || *size < min_tree_dimension) {
            return {std::nullopt, std::string(key) + ": not a size of at least 0.01"};
        }
        tree.shape.*dimension = *size;
    }
    return {tree, ""};
}
}

parsed<std::vector<material_zone>> material_zones_from_json(const json& root) {
    return json_elements<material_zone>(root, "material_zones", material_zone_from_json);
}

parsed<std::vector<tree_instance>> trees_from_json(const json& root) {
    return json_elements<tree_instance>(root, "trees", tree_from_json);
}

// A bush: its `position` on the ground, its `radius` and `height`; all leaves.
parsed<tree_instance> bush_from_json(const json& object) {
    const std::optional<glm::vec3> position = json_vec3(object, "position");
    const std::optional<float> radius = json_float(object, "radius");
    const std::optional<float> height = json_float(object, "height");
    if (!position) {
        return {std::nullopt, "position: not [x, y, z]"};
    }
    if (!radius || !height || *radius < min_tree_dimension || *height < min_tree_dimension) {
        return {std::nullopt, "radius and height: not sizes of at least 0.01"};
    }
    tree_instance bush;
    bush.position = *position;
    bush.shape = tree_shape{0.0f, 0.0f, *radius, *height, tree_kind::bush};
    bush.species = "scrub";
    return {bush, ""};
}

parsed<hole_data> parse_hole_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return {std::nullopt, "not a JSON object"};
    }

    const std::optional<glm::vec3> tee = json_vec3(*root, "tee");
    const std::optional<glm::vec3> pin = json_vec3(*root, "pin");
    if (!tee || !pin) {
        return {std::nullopt, !tee ? "tee: not [x, y, z]" : "pin: not [x, y, z]"};
    }
    const json* spline = json_object(*root, "spline");
    const std::optional<float> width = spline != nullptr ? json_float(*spline, "width") : std::nullopt;
    if (!width || *width <= 0.0f) {
        return {std::nullopt, "spline.width: not a number above 0"};
    }
    const json* control_points_json = json_array(*spline, "control_points");
    const std::optional<std::vector<glm::vec3>> control_points =
        control_points_json != nullptr ? json_vec3_array(*control_points_json) : std::nullopt;
    if (!control_points || control_points->size() < 2) {
        return {std::nullopt, "spline.control_points: not two or more [x, y, z]"};
    }
    // Optional; when given, one number per control point.
    std::vector<float> bank;
    if (spline->contains("bank")) {
        const json* bank_json = json_array(*spline, "bank");
        if (bank_json == nullptr || bank_json->size() != control_points->size()) {
            return {std::nullopt, "spline.bank: not one number per control point"};
        }
        for (std::size_t i = 0; i < bank_json->size(); ++i) {
            const std::optional<float> angle = json_float((*bank_json)[i]);
            if (!angle) {
                return {std::nullopt, "spline.bank[" + std::to_string(i) + "]: not a number"};
            }
            bank.push_back(*angle);
        }
    }
    const std::optional<float> rough_width = json_float_or(*spline, "rough_width", *width);
    if (!rough_width) {
        return {std::nullopt, "spline.rough_width: not a number"};
    }
    const std::optional<int> par = json_int_or(*root, "par", default_hole_par);
    if (!par || *par < 1) {
        return {std::nullopt, "par: not a whole number above 0"};
    }
    const std::optional<bool> tee_box = json_bool_or(*root, "tee_box", true);
    const std::optional<bool> ribbon_fairway = json_bool_or(*root, "ribbon_fairway", true);
    if (!tee_box || !ribbon_fairway) {
        return {std::nullopt, !tee_box ? "tee_box: not true or false" : "ribbon_fairway: not true or false"};
    }
    parsed<std::vector<material_zone>> zones = material_zones_from_json(*root);
    if (!zones.value) {
        return {std::nullopt, zones.error};
    }
    parsed<std::vector<tree_instance>> trees = trees_from_json(*root);
    if (!trees.value) {
        return {std::nullopt, trees.error};
    }
    parsed<std::vector<tree_instance>> bushes = json_elements<tree_instance>(*root, "bushes", bush_from_json);
    if (!bushes.value) {
        return {std::nullopt, bushes.error};
    }
    trees.value->insert(trees.value->end(), bushes.value->begin(), bushes.value->end());

    hole_data hole;
    hole.id = json_string(*root, "id").value_or("");
    hole.name = json_string(*root, "name").value_or(hole.id);
    hole.par = *par;
    hole.tee_position = *tee;
    hole.pin_position = *pin;
    hole.spline.width = *width;
    hole.spline.rough_width = std::max(*width, *rough_width);
    hole.spline.control_points = *control_points;
    hole.spline.bank = std::move(bank);
    hole.material_zones = std::move(*zones.value);
    hole.trees = std::move(*trees.value);
    hole.tee_box = *tee_box;
    hole.ribbon_fairway = *ribbon_fairway;
    return {std::move(hole), ""};
}
