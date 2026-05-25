#include "game/hole_loader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

std::optional<json> load_json_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    json parsed = json::parse(file, nullptr, false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string> string_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<int> int_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    return it->get<int>();
}

std::optional<std::uint32_t> uint_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return std::nullopt;
    }
    return it->get<std::uint32_t>();
}

std::optional<float> float_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<float>();
}

std::optional<glm::vec3> vec3_from_json(const json& value) {
    if (!value.is_array() || value.size() != 3) {
        return std::nullopt;
    }

    for (const json& component : value) {
        if (!component.is_number()) {
            return std::nullopt;
        }
    }

    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

std::optional<std::vector<glm::vec3>> vec3_array_from_json(const json& value) {
    if (!value.is_array()) {
        return std::nullopt;
    }

    std::vector<glm::vec3> points;
    points.reserve(value.size());
    for (const json& element : value) {
        std::optional<glm::vec3> point = vec3_from_json(element);
        if (!point) {
            return std::nullopt;
        }
        points.push_back(*point);
    }
    return points;
}

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

std::vector<material_zone> material_zones_from_json(const json& root) {
    std::vector<material_zone> zones;
    const auto array_it = root.find("material_zones");
    if (array_it == root.end() || !array_it->is_array()) {
        return zones;
    }

    for (const json& object : *array_it) {
        if (!object.is_object()) {
            continue;
        }

        material_zone zone;
        zone.type = material_type_from_string(string_at(object, "type").value_or(""));

        const auto center_it = object.find("center");
        const std::optional<float> radius = float_at(object, "radius");
        if (center_it != object.end() && radius) {
            const std::optional<glm::vec3> parsed_center = vec3_from_json(*center_it);
            if (parsed_center) {
                zone.center = *parsed_center;
                zone.radius = *radius;
                zone.has_radius = true;
            }
        }

        const auto bounds_it = object.find("bounds");
        if (bounds_it != object.end()) {
            const std::optional<std::vector<glm::vec3>> corners = vec3_array_from_json(*bounds_it);
            if (corners && corners->size() >= 2) {
                zone.bounds_min = glm::vec3(std::min((*corners)[0].x, (*corners)[1].x),
                                            std::min((*corners)[0].y, (*corners)[1].y),
                                            std::min((*corners)[0].z, (*corners)[1].z));
                zone.bounds_max = glm::vec3(std::max((*corners)[0].x, (*corners)[1].x),
                                            std::max((*corners)[0].y, (*corners)[1].y),
                                            std::max((*corners)[0].z, (*corners)[1].z));
                zone.has_bounds = true;
            }
        }

        zones.push_back(zone);
    }

    return zones;
}

std::vector<tree_instance> trees_from_json(const json& root) {
    std::vector<tree_instance> trees;
    const auto array_it = root.find("trees");
    if (array_it == root.end() || !array_it->is_array()) {
        return trees;
    }

    for (const json& object : *array_it) {
        if (!object.is_object()) {
            continue;
        }

        const auto position_it = object.find("position");
        if (position_it == object.end()) {
            continue;
        }

        const std::optional<glm::vec3> position = vec3_from_json(*position_it);
        if (!position) {
            continue;
        }

        tree_instance tree;
        tree.position = *position;
        tree.trunk_radius = std::max(0.01f, float_at(object, "trunk_radius").value_or(tree.trunk_radius));
        tree.trunk_height = std::max(0.01f, float_at(object, "trunk_height").value_or(tree.trunk_height));
        tree.leaf_radius = std::max(0.01f, float_at(object, "leaf_radius").value_or(tree.leaf_radius));
        tree.leaf_height = std::max(0.01f, float_at(object, "leaf_height").value_or(tree.leaf_height));
        trees.push_back(tree);
    }

    return trees;
}

float extent_for_point(const glm::vec3& point, const float padding) {
    return std::max(std::abs(point.x), std::abs(point.z)) + padding;
}
}

std::optional<hole_data> load_hole_from_file(const std::string& path) {
    const std::optional<json> root = load_json_file(path);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }

    const auto tee_it = root->find("tee");
    const auto pin_it = root->find("pin");
    const auto spline_it = root->find("spline");
    if (tee_it == root->end() || pin_it == root->end() || spline_it == root->end() || !spline_it->is_object()) {
        return std::nullopt;
    }

    const std::optional<glm::vec3> tee = vec3_from_json(*tee_it);
    const std::optional<glm::vec3> pin = vec3_from_json(*pin_it);
    const auto control_points_it = spline_it->find("control_points");
    const std::optional<float> width = float_at(*spline_it, "width");
    if (!tee || !pin || control_points_it == spline_it->end() || !width || *width <= 0.0f) {
        return std::nullopt;
    }

    const std::optional<std::vector<glm::vec3>> control_points = vec3_array_from_json(*control_points_it);
    if (!control_points || control_points->size() < 2) {
        return std::nullopt;
    }

    hole_data hole;
    hole.id = string_at(*root, "id").value_or("");
    hole.name = string_at(*root, "name").value_or(hole.id);
    hole.par = int_at(*root, "par").value_or(3);
    hole.wind_seed = uint_at(*root, "wind_seed").value_or(42U);
    hole.tee_position = *tee;
    hole.pin_position = *pin;
    hole.spline.width = *width;
    hole.spline.rough_width = std::max(*width, float_at(*spline_it, "rough_width").value_or(*width));
    hole.spline.control_points = *control_points;
    hole.material_zones = material_zones_from_json(*root);
    hole.trees = trees_from_json(*root);

    return hole;
}

std::vector<hole_data> load_holes_from_directory(const std::string& directory) {
    std::vector<hole_data> holes;
    const std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return holes;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        const std::optional<hole_data> hole = load_hole_from_file(path.string());
        if (hole) {
            holes.push_back(*hole);
        }
    }

    return holes;
}

float estimate_course_extent(const hole_data& hole) {
    const float course_width = std::max(hole.spline.width, hole.spline.rough_width);
    float extent = std::max(extent_for_point(hole.tee_position, course_width),
                            extent_for_point(hole.pin_position, course_width));

    for (const glm::vec3& point : hole.spline.control_points) {
        extent = std::max(extent, extent_for_point(point, course_width));
    }

    for (const material_zone& zone : hole.material_zones) {
        if (zone.has_radius) {
            extent = std::max(extent, extent_for_point(zone.center, zone.radius));
        }
        if (zone.has_bounds) {
            extent = std::max(extent, extent_for_point(zone.bounds_min, 0.0f));
            extent = std::max(extent, extent_for_point(zone.bounds_max, 0.0f));
        }
    }

    for (const tree_instance& tree : hole.trees) {
        extent = std::max(extent, extent_for_point(tree.position, tree.leaf_radius));
    }

    return std::max(20.0f, extent);
}
