#include "game/course_world_loader.h"

#include "game/json_util.h"

#include <algorithm>

namespace {
std::optional<course_world_hole_start> hole_start_from_json(const json& value, const course_definition& course) {
    const std::optional<int> hole_index = json_int(value, "hole_index");
    const std::optional<glm::vec3> position = json_vec3(value, "position");
    const std::optional<glm::vec3> return_position = json_vec3(value, "return_position");
    if (!hole_index || *hole_index < 0 || *hole_index >= static_cast<int>(course.holes.size()) ||
        !position || !return_position) {
        return std::nullopt;
    }

    course_world_hole_start start;
    start.hole_index = *hole_index;
    start.position = *position;
    start.return_position = *return_position;
    start.interaction_radius = json_float(value, "interaction_radius").value_or(default_hole_start_radius);
    start.rotation_degrees = json_float(value, "rotation_degrees").value_or(0.0f);
    return start;
}

// The land under the course. Every cell must have a height.
std::optional<height_grid> ground_from_json(const json& root) {
    const json* ground = json_object(root, "ground");
    if (ground == nullptr) {
        return std::nullopt;
    }
    const json* origin = json_array(*ground, "origin");
    const std::optional<float> cell_size = json_float(*ground, "cell_size");
    const std::optional<int> columns = json_int(*ground, "columns");
    const std::optional<int> rows = json_int(*ground, "rows");
    const json* heights = json_array(*ground, "heights");
    if (origin == nullptr || origin->size() != 2 || !(*origin)[0].is_number() || !(*origin)[1].is_number() ||
        !cell_size || !(*cell_size > 0.0f) || !columns || *columns < 2 || !rows || *rows < 2 || heights == nullptr ||
        heights->size() != static_cast<std::size_t>(*columns) * static_cast<std::size_t>(*rows)) {
        return std::nullopt;
    }

    height_grid grid;
    grid.origin_x = (*origin)[0].get<float>();
    grid.origin_z = (*origin)[1].get<float>();
    grid.cell_size = *cell_size;
    grid.columns = *columns;
    grid.rows = *rows;
    grid.heights.reserve(heights->size());
    for (const json& height : *heights) {
        if (!height.is_number()) {
            return std::nullopt;
        }
        grid.heights.push_back(height.get<float>());
    }
    return grid;
}

std::vector<course_world_cart_road> cart_roads_from_json(const json& root) {
    std::vector<course_world_cart_road> roads;
    const json* array = json_array(root, "cart_roads");
    if (array == nullptr) {
        return roads;
    }
    for (const json& value : *array) {
        const json* polyline = json_array(value, "polyline");
        const std::optional<std::vector<glm::vec3>> points = polyline != nullptr ? json_vec3_array(*polyline) : std::nullopt;
        if (!points || points->size() < 2) {
            continue;
        }
        roads.push_back(course_world_cart_road{json_float(value, "width").value_or(default_cart_road_width), *points});
    }
    return roads;
}

std::vector<course_world_skill_reward> skill_rewards_from_json(const json& value) {
    std::vector<course_world_skill_reward> rewards;
    if (!value.is_object()) {
        return rewards;
    }
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (it.value().is_number_integer() && !it.key().empty() && it.value().get<int>() > 0) {
            rewards.push_back(course_world_skill_reward{it.key(), it.value().get<int>()});
        }
    }
    return rewards;
}

std::optional<course_world_collectible> collectible_from_json(const json& value) {
    const std::string id = json_string(value, "id").value_or("");
    const std::optional<glm::vec3> position = json_vec3(value, "position");
    if (id.empty() || !position) {
        return std::nullopt;
    }

    course_world_collectible collectible;
    collectible.id = id;
    collectible.position = *position;
    collectible.interaction_radius = json_float(value, "interaction_radius").value_or(default_collectible_radius);
    collectible.repeatable = json_bool(value, "repeatable").value_or(false);
    collectible.repeatable_cooldown_holes = std::max(0, json_int(value, "repeatable_cooldown_holes").value_or(0));

    if (const json* reward = json_object(value, "reward")) {
        collectible.world_flag = json_string(*reward, "world_flag").value_or("");
        if (const json* skills = json_object(*reward, "skill_xp")) {
            collectible.skill_rewards = skill_rewards_from_json(*skills);
        }
    }

    if (const json* requirement = json_object(value, "requirement")) {
        collectible.requirement.skill_id = json_string(*requirement, "skill_id").value_or("");
        collectible.requirement.min_level = std::max(1, json_int(*requirement, "min_level").value_or(1));
        collectible.requirement.required_world_flag = json_string(*requirement, "required_world_flag").value_or("");
        collectible.requirement.required_completed_course_id =
            json_string(*requirement, "required_completed_course_id").value_or("");
    }
    return collectible;
}
}

std::optional<course_world_definition> parse_course_world_from_text(const std::string& text,
                                                                    const course_definition& course) {
    const std::optional<json> root = parse_json(text);
    const json* starts = root ? json_array(*root, "hole_starts") : nullptr;
    if (starts == nullptr || starts->size() != course.holes.size()) {
        return std::nullopt;
    }

    course_world_definition world;
    world.id = json_string(*root, "id").value_or("");
    world.name = json_string(*root, "name").value_or(world.id);

    for (const json& value : *starts) {
        const std::optional<course_world_hole_start> start = hole_start_from_json(value, course);
        if (!start) {
            return std::nullopt;
        }
        world.hole_starts.push_back(*start);
    }
    std::sort(world.hole_starts.begin(), world.hole_starts.end(),
              [](const course_world_hole_start& a, const course_world_hole_start& b) { return a.hole_index < b.hole_index; });
    for (std::size_t i = 0; i < world.hole_starts.size(); ++i) {
        if (world.hole_starts[i].hole_index != static_cast<int>(i)) {
            return std::nullopt;
        }
    }

    std::optional<height_grid> ground = ground_from_json(*root);
    if (!ground) {
        return std::nullopt;
    }
    world.ground = std::move(*ground);
    world.cart_roads = cart_roads_from_json(*root);
    if (const json* collectibles = json_array(*root, "collectibles")) {
        for (const json& value : *collectibles) {
            if (std::optional<course_world_collectible> collectible = collectible_from_json(value)) {
                world.collectibles.push_back(std::move(*collectible));
            }
        }
    }
    return world;
}

