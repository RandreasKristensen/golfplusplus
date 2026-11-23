#include "game/tree_looks.h"

#include <algorithm>
#include <utility>

#include <glm/vector_relational.hpp>

namespace {
std::optional<crown_shape> crown_shape_named(const std::string& name) {
    const std::pair<const char*, crown_shape> shapes[] = {
        {"round", crown_shape::round},     {"oval", crown_shape::oval},       {"cone", crown_shape::cone},
        {"umbrella", crown_shape::umbrella}, {"weeping", crown_shape::weeping}, {"dome", crown_shape::dome},
    };
    for (const auto& [key, shape] : shapes) {
        if (name == key) {
            return shape;
        }
    }
    return std::nullopt;
}

std::optional<float> ranged(const json& object, const char* key, const float low, const float high) {
    const std::optional<float> value = json_float(object, key);
    return value && *value >= low && *value <= high ? value : std::nullopt;
}

std::optional<int> counted(const json& object, const char* key, const int low, const int high) {
    const std::optional<int> value = json_int(object, key);
    return value && *value >= low && *value <= high ? value : std::nullopt;
}

std::optional<glm::vec3> colour(const json& object, const char* key) {
    const std::optional<glm::vec3> value = json_vec3(object, key);
    if (!value || glm::any(glm::lessThan(*value, glm::vec3(0.0f))) || glm::any(glm::greaterThan(*value, glm::vec3(1.0f)))) {
        return std::nullopt;
    }
    return value;
}

std::optional<tree_look> look_from_json(const json& object) {
    tree_look look;
    look.name = json_string(object, "name").value_or("");
    const std::optional<crown_shape> shape = crown_shape_named(json_string(object, "shape").value_or(""));
    const std::optional<float> trunk_share = ranged(object, "trunk_share", 0.0f, 0.9f);
    const std::optional<float> trunk_radius = ranged(object, "trunk_radius", 0.01f, 0.5f);
    const std::optional<int> stems = counted(object, "stems", 1, 12);
    const std::optional<float> apical = ranged(object, "apical_dominance", 0.0f, 1.0f);
    const std::optional<float> light_bend = ranged(object, "light_bend", 0.0f, 2.0f);
    const std::optional<float> branch_droop = ranged(object, "branch_droop", 0.0f, 2.0f);
    const std::optional<float> twig_spacing = ranged(object, "twig_spacing", 0.04f, 0.5f);
    const std::optional<float> pipe_exponent = ranged(object, "pipe_exponent", 1.5f, 3.0f);
    const std::optional<int> clumps = counted(object, "clumps", 1, 400);
    const std::optional<float> clump_size = ranged(object, "clump_size", 0.02f, 1.0f);
    const std::optional<float> droop = ranged(object, "droop", 0.0f, 4.0f);
    const std::optional<float> aspect = ranged(object, "aspect", 0.2f, 10.0f);
    const std::optional<float> twig_kink = ranged(object, "twig_kink", 0.0f, 1.0f);
    const std::optional<float> twig_hang = ranged(object, "twig_hang", 0.0f, 1.0f);
    const std::optional<float> twig_haze = ranged(object, "twig_haze", 0.0f, 1.0f);
    const std::optional<float> shine_through = ranged(object, "shine_through", 0.0f, 1.0f);
    const std::optional<glm::vec3> bark = colour(object, "bark");
    const std::optional<glm::vec3> twig = colour(object, "twig");
    const std::optional<glm::vec3> spring = colour(object, "spring");
    const std::optional<glm::vec3> summer = colour(object, "summer");
    const std::optional<glm::vec3> autumn = colour(object, "autumn");
    const std::optional<glm::vec3> withered = colour(object, "withered");
    if (look.name.empty() || !shape || !trunk_share || !trunk_radius || !stems || !apical || !light_bend || !branch_droop ||
        !twig_spacing || !pipe_exponent || !clumps || !clump_size || !droop || !aspect || !twig_kink || !twig_hang || !twig_haze ||
        !shine_through || !bark || !twig || !spring || !summer || !autumn || !withered) {
        return std::nullopt;
    }
    look.shape = *shape;
    look.trunk_share = *trunk_share;
    look.trunk_radius = *trunk_radius;
    look.stems = *stems;
    look.apical_dominance = *apical;
    look.light_bend = *light_bend;
    look.branch_droop = *branch_droop;
    look.twig_spacing = *twig_spacing;
    look.pipe_exponent = *pipe_exponent;
    look.clumps = *clumps;
    look.clump_size = *clump_size;
    look.droop = *droop;
    look.aspect = *aspect;
    look.twig_kink = *twig_kink;
    look.twig_hang = *twig_hang;
    look.twig_haze = *twig_haze;
    look.shine_through = *shine_through;
    look.bark = *bark;
    look.twig = *twig;
    look.spring = *spring;
    look.summer = *summer;
    look.autumn = *autumn;
    look.withered = *withered;
    return look;
}
}

std::optional<std::vector<tree_look>> parse_tree_looks(const json& root) {
    const json* trees = json_object(root, "trees");
    const json* species = trees != nullptr ? json_array(*trees, "species") : nullptr;
    if (species == nullptr || species->empty()) {
        return std::nullopt;
    }
    std::vector<tree_look> looks;
    for (const json& entry : *species) {
        std::optional<tree_look> look = look_from_json(entry);
        if (!look || std::any_of(looks.begin(), looks.end(), [&](const tree_look& other) { return other.name == look->name; })) {
            return std::nullopt;
        }
        looks.push_back(std::move(*look));
    }
    return looks;
}

std::optional<tree_impostor_style> parse_tree_impostor_style(const json& root) {
    const json* trees = json_object(root, "trees");
    const json* object = trees != nullptr ? json_object(*trees, "impostor") : nullptr;
    if (object == nullptr) {
        return std::nullopt;
    }
    const std::optional<int> azimuths = counted(*object, "azimuths", 1, 16);
    const std::optional<int> tile = counted(*object, "tile_pixels", 8, 256);
    const std::optional<float> radius_error = ranged(*object, "radius_error", 0.0f, 1.0f);
    const std::optional<float> wind_error = ranged(*object, "wind_error", 0.0f, 1.0f);
    const std::optional<float> max_error = ranged(*object, "max_error_pixels", 0.0f, 8.0f);
    const std::optional<float> depth_first = ranged(*object, "depth_first_coverage", 0.0f, 64.0f);
    const std::optional<float> depth_first_lamps = ranged(*object, "depth_first_coverage_lamps", 0.0f, 64.0f);
    const json* elevations = json_array(*object, "elevations_degrees");
    if (!depth_first || !depth_first_lamps || !azimuths || !tile || !radius_error || !wind_error || !max_error || elevations == nullptr ||
        elevations->empty() || elevations->size() > 8) {
        return std::nullopt;
    }
    tree_impostor_style style;
    style.azimuths = *azimuths;
    style.tile_pixels = *tile;
    style.radius_error = *radius_error;
    style.wind_error = *wind_error;
    style.max_error_pixels = *max_error;
    style.depth_first_coverage = *depth_first;
    style.depth_first_coverage_lamps = *depth_first_lamps;
    for (const json& entry : *elevations) {
        const std::optional<float> degrees = json_float(entry);
        if (!degrees || *degrees < -80.0f || *degrees > 80.0f ||
            (!style.elevations_degrees.empty() && *degrees <= style.elevations_degrees.back())) {
            return std::nullopt;
        }
        style.elevations_degrees.push_back(*degrees);
    }
    return style;
}

std::optional<falling_leaf_style> parse_falling_leaf_style(const json& root) {
    const json* trees = json_object(root, "trees");
    const json* object = trees != nullptr ? json_object(*trees, "falling_leaves") : nullptr;
    if (object == nullptr) {
        return std::nullopt;
    }
    const std::optional<float> per_square_meter = ranged(*object, "leaves_per_square_meter", 0.0f, 100000.0f);
    const std::optional<float> size = ranged(*object, "leaf_size_meters", 0.005f, 0.5f);
    const std::optional<float> speed = ranged(*object, "fall_speed", 0.05f, 10.0f);
    const std::optional<float> flutter_seconds = ranged(*object, "flutter_seconds", 0.05f, 10.0f);
    const std::optional<float> flutter_meters = ranged(*object, "flutter_meters", 0.0f, 5.0f);
    const std::optional<float> lie = ranged(*object, "lie_seconds", 0.0f, 120.0f);
    const std::optional<float> wind = ranged(*object, "wind_factor", 0.0f, 10.0f);
    const std::optional<int> per_tree = counted(*object, "most_per_tree", 0, 256);
    const std::optional<int> per_pass = counted(*object, "most_per_pass", 0, 1000000);
    const std::optional<float> pixels = ranged(*object, "min_pixels", 0.1f, 16.0f);
    if (!per_square_meter || !size || !speed || !flutter_seconds || !flutter_meters || !lie || !wind || !per_tree || !per_pass ||
        !pixels) {
        return std::nullopt;
    }
    return falling_leaf_style{*per_square_meter, *size, *speed, *flutter_seconds, *flutter_meters, *lie, *wind, *per_tree,
                              *per_pass, *pixels};
}
