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
    const std::optional<int> limbs = counted(object, "limbs", 1, 32);
    const std::optional<int> clumps = counted(object, "clumps", 1, 400);
    const std::optional<int> far_clumps = counted(object, "far_clumps", 1, 400);
    const std::optional<float> clump_size = ranged(object, "clump_size", 0.02f, 1.0f);
    const std::optional<float> droop = ranged(object, "droop", 0.0f, 4.0f);
    const std::optional<float> aspect = ranged(object, "aspect", 0.2f, 10.0f);
    const std::optional<float> shine_through = ranged(object, "shine_through", 0.0f, 1.0f);
    const std::optional<glm::vec3> bark = colour(object, "bark");
    const std::optional<glm::vec3> spring = colour(object, "spring");
    const std::optional<glm::vec3> summer = colour(object, "summer");
    const std::optional<glm::vec3> autumn = colour(object, "autumn");
    if (look.name.empty() || !shape || !trunk_share || !trunk_radius || !limbs || !clumps || !far_clumps || !clump_size ||
        !droop || !aspect || !shine_through || !bark || !spring || !summer || !autumn) {
        return std::nullopt;
    }
    look.shape = *shape;
    look.trunk_share = *trunk_share;
    look.trunk_radius = *trunk_radius;
    look.limbs = *limbs;
    look.clumps = *clumps;
    look.far_clumps = *far_clumps;
    look.clump_size = *clump_size;
    look.droop = *droop;
    look.aspect = *aspect;
    look.shine_through = *shine_through;
    look.bark = *bark;
    look.spring = *spring;
    look.summer = *summer;
    look.autumn = *autumn;
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
