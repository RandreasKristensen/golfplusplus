#include "game/course_world_loader.h"

#include "game/json_util.h"
#include "game/hole_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace {
parsed<course_world_hole_start> hole_start_from_json(const json& value, const course_definition& course) {
    const std::optional<int> hole_index = json_int(value, "hole_index");
    if (!hole_index || *hole_index < 0 || *hole_index >= static_cast<int>(course.holes.size())) {
        return {std::nullopt, "hole_index: not a hole of the course"};
    }
    const std::optional<glm::vec3> position = json_vec3(value, "position");
    if (!position) {
        return {std::nullopt, "position: not [x, y, z]"};
    }
    const std::optional<float> radius = json_float_or(value, "interaction_radius", default_hole_start_radius);
    const std::optional<float> rotation = json_float_or(value, "rotation_degrees", 0.0f);
    if (!radius || !rotation) {
        return {std::nullopt, !radius ? "interaction_radius: not a number" : "rotation_degrees: not a number"};
    }

    course_world_hole_start start;
    start.hole_index = *hole_index;
    start.position = *position;
    start.interaction_radius = *radius;
    start.rotation_degrees = *rotation;
    return {start, ""};
}

// Heights stay within this many centimetres of zero, where a float holds
// every whole centimetre: far beyond any land, and no sum can overflow.
constexpr std::int64_t max_ground_height_cm = std::int64_t{1} << 24;

// Whether a grid of `cells` spaced `cell_size` from `origin` ends at a finite float.
bool grid_extent_finite(const float origin, const float cell_size, const int cells) {
    const double end = static_cast<double>(origin) + static_cast<double>(cell_size) * static_cast<double>(cells - 1);
    return std::abs(end) <= static_cast<double>(std::numeric_limits<float>::max());
}

// The land under the course. `heights_cm` holds one array per row of the
// grid, in whole centimetres: a row's first entry is its height, every next
// one the step from the one before (small numbers, so a fine grid stays a
// small file). Every cell must have a height.
parsed<height_grid> ground_from_json(const json& root) {
    const json* ground = json_object(root, "ground");
    if (ground == nullptr) {
        return {std::nullopt, "ground: missing"};
    }
    const std::optional<glm::vec2> origin = json_vec2(*ground, "origin");
    const std::optional<float> cell_size = json_float(*ground, "cell_size");
    const std::optional<int> columns = json_int(*ground, "columns");
    const std::optional<int> rows = json_int(*ground, "rows");
    const json* height_rows = json_array(*ground, "heights_cm");
    if (!origin || !cell_size || !(*cell_size > 0.0f)) {
        return {std::nullopt, !origin ? "ground.origin: not [x, z]" : "ground.cell_size: not a number above 0"};
    }
    if (!columns || *columns < 2 || !rows || *rows < 2) {
        return {std::nullopt, "ground: columns and rows not whole numbers of at least 2"};
    }
    if (!grid_extent_finite(origin->x, *cell_size, *columns) || !grid_extent_finite(origin->y, *cell_size, *rows)) {
        return {std::nullopt, "ground: reaches beyond any float"};
    }
    if (height_rows == nullptr || height_rows->size() != static_cast<std::size_t>(*rows)) {
        return {std::nullopt, "ground.heights_cm: not one array per row"};
    }
    // Every row is checked against `columns` before the grid is allocated.
    for (std::size_t row = 0; row < height_rows->size(); ++row) {
        const json& heights = (*height_rows)[row];
        if (!heights.is_array() || heights.size() != static_cast<std::size_t>(*columns)) {
            return {std::nullopt, "ground.heights_cm[" + std::to_string(row) + "]: not one height per column"};
        }
    }

    height_grid grid;
    grid.origin_x = origin->x;
    grid.origin_z = origin->y;
    grid.cell_size = *cell_size;
    grid.columns = *columns;
    grid.rows = *rows;
    grid.heights.reserve(static_cast<std::size_t>(*columns) * static_cast<std::size_t>(*rows));
    for (std::size_t row = 0; row < height_rows->size(); ++row) {
        std::int64_t centimetres = 0;
        const json& steps = (*height_rows)[row];
        for (std::size_t column = 0; column < steps.size(); ++column) {
            const std::optional<std::int64_t> delta = json_int64(steps[column]);
            // A step beyond twice the limit cannot land within it; the sum then cannot overflow.
            if (!delta || *delta < -2 * max_ground_height_cm || *delta > 2 * max_ground_height_cm ||
                std::abs(centimetres + *delta) > max_ground_height_cm) {
                return {std::nullopt, "ground.heights_cm[" + std::to_string(row) + "][" + std::to_string(column) +
                                          "]: not whole centimetres within " + std::to_string(max_ground_height_cm) + " of 0"};
            }
            centimetres += *delta;
            grid.heights.push_back(static_cast<float>(centimetres) * 0.01f);
        }
    }
    return {std::move(grid), ""};
}

parsed<course_world_cart_road> cart_road_from_json(const json& value) {
    const json* polyline = json_array(value, "polyline");
    const std::optional<std::vector<glm::vec3>> points = polyline != nullptr ? json_vec3_array(*polyline) : std::nullopt;
    if (!points || points->size() < 2) {
        return {std::nullopt, "polyline: not two or more [x, y, z]"};
    }
    const std::optional<float> width = json_float_or(value, "width", default_cart_road_width);
    if (!width || !(*width > 0.0f)) {
        return {std::nullopt, "width: not a number above 0"};
    }
    return {course_world_cart_road{*width, *points}, ""};
}

parsed<course_world_fence> fence_from_json(const json& value) {
    const json* poles = json_array(value, "poles");
    const std::optional<std::vector<glm::vec3>> points = poles != nullptr ? json_vec3_array(*poles) : std::nullopt;
    if (!points || points->size() < 2) {
        return {std::nullopt, "poles: not two or more [x, y, z]"};
    }
    const std::optional<float> height = json_float(value, "height");
    if (!height || !(*height > 0.0f)) {
        return {std::nullopt, "height: not a number above 0"};
    }
    return {course_world_fence{*points, *height}, ""};
}

// `skill_xp`: skill id to a whole number of XP above 0.
parsed<std::vector<course_world_skill_reward>> skill_rewards_from_json(const json& value) {
    std::vector<course_world_skill_reward> rewards;
    for (auto it = value.begin(); it != value.end(); ++it) {
        const std::optional<int> xp = json_int(it.value());
        if (it.key().empty() || !xp || *xp <= 0) {
            return {std::nullopt, "reward.skill_xp." + it.key() + ": not a skill and whole XP above 0"};
        }
        rewards.push_back(course_world_skill_reward{it.key(), *xp});
    }
    return {std::move(rewards), ""};
}

parsed<course_world_collectible> collectible_from_json(const json& value) {
    const std::string id = json_string(value, "id").value_or("");
    const std::optional<glm::vec3> position = json_vec3(value, "position");
    if (id.empty() || !position) {
        return {std::nullopt, id.empty() ? "id: not a name" : "position: not [x, y, z]"};
    }
    const std::optional<float> radius = json_float_or(value, "interaction_radius", default_collectible_radius);
    const std::optional<bool> repeatable = json_bool_or(value, "repeatable", false);
    const std::optional<int> cooldown = json_int_or(value, "repeatable_cooldown_holes", 0);
    if (!radius || !repeatable || !cooldown || *cooldown < 0) {
        return {std::nullopt, !radius ? "interaction_radius: not a number"
                              : !repeatable ? "repeatable: not true or false" : "repeatable_cooldown_holes: not a whole number of at least 0"};
    }

    course_world_collectible collectible;
    collectible.id = id;
    collectible.position = *position;
    collectible.interaction_radius = *radius;
    collectible.repeatable = *repeatable;
    collectible.repeatable_cooldown_holes = *cooldown;

    if (const json* reward = json_object(value, "reward")) {
        collectible.world_flag = json_string(*reward, "world_flag").value_or("");
        if (const json* skills = json_object(*reward, "skill_xp")) {
            parsed<std::vector<course_world_skill_reward>> rewards = skill_rewards_from_json(*skills);
            if (!rewards.value) {
                return {std::nullopt, rewards.error};
            }
            collectible.skill_rewards = std::move(*rewards.value);
        }
    }

    if (const json* requirement = json_object(value, "requirement")) {
        const std::optional<int> min_level = json_int_or(*requirement, "min_level", 1);
        if (!min_level || *min_level < 1) {
            return {std::nullopt, "requirement.min_level: not a whole number above 0"};
        }
        collectible.requirement.skill_id = json_string(*requirement, "skill_id").value_or("");
        collectible.requirement.min_level = *min_level;
        collectible.requirement.required_world_flag = json_string(*requirement, "required_world_flag").value_or("");
        collectible.requirement.required_completed_course_id =
            json_string(*requirement, "required_completed_course_id").value_or("");
    }
    return {std::move(collectible), ""};
}

std::optional<hub_anchor> anchor_from_json(const json& object, const char* key) {
    const json* value = json_object(object, key);
    const std::optional<glm::vec3> position = value != nullptr ? json_vec3(*value, "position") : std::nullopt;
    const std::optional<float> reach = value != nullptr ? json_float(*value, "reach") : std::nullopt;
    if (!position || !reach || *reach <= 0.0f) {
        return std::nullopt;
    }
    return hub_anchor{*position, *reach};
}
}

parsed<course_world_definition> parse_world_from_text(const std::string& text, const course_definition* course) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return {std::nullopt, "not a JSON object"};
    }

    course_world_definition world;
    if (const json* hub = json_object(*root, "hub")) {
        const std::optional<hub_anchor> book = anchor_from_json(*hub, "book");
        if (!book || course) {
            return {std::nullopt, course ? "hub: in a course's world" : "hub.book: not a position and a reach above 0"};
        }
        world.hub = hub_anchors{*book};
    }
    if (root->contains("bus_stop")) {
        const auto stop = json_object(*root, "bus_stop");
        const auto position = stop ? json_vec3(*stop, "position") : std::nullopt;
        const auto heading = stop ? json_float(*stop, "heading") : std::nullopt;
        if (!position || !heading) {
            return {std::nullopt, "bus_stop: not a position and a heading"};
        }
        world.bus_stop = bus_stop{*position, *heading};
    }
    world.id = json_string(*root, "id").value_or("");
    world.name = json_string(*root, "name").value_or(world.id);

    if (course) {
        world.day_of_year = day_of_year(course->sky.month, course->sky.day);
        const json* starts = json_array(*root, "hole_starts");
        if (starts == nullptr || starts->size() != course->holes.size()) {
            return {std::nullopt, "hole_starts: not one per hole"};
        }
        for (std::size_t i = 0; i < starts->size(); ++i) {
            parsed<course_world_hole_start> start = hole_start_from_json((*starts)[i], *course);
            if (!start.value) {
                return {std::nullopt, "hole_starts[" + std::to_string(i) + "]." + start.error};
            }
            world.hole_starts.push_back(*start.value);
        }
        std::sort(world.hole_starts.begin(), world.hole_starts.end(),
                  [](const course_world_hole_start& a, const course_world_hole_start& b) { return a.hole_index < b.hole_index; });
        for (std::size_t i = 0; i < world.hole_starts.size(); ++i) {
            if (world.hole_starts[i].hole_index != static_cast<int>(i)) {
                return {std::nullopt, "hole_starts: hole " + std::to_string(i) + " has no start of its own"};
            }
        }
    }

    parsed<std::vector<material_zone>> zones = material_zones_from_json(*root);
    if (!zones.value) {
        return {std::nullopt, zones.error};
    }
    world.zones = std::move(*zones.value);
    parsed<std::vector<tree_instance>> trees = trees_from_json(*root);
    if (!trees.value) {
        return {std::nullopt, trees.error};
    }
    world.trees = std::move(*trees.value);

    const json* projection = json_object(*root, "projection");
    const std::optional<float> latitude = projection != nullptr ? json_float(*projection, "origin_lat") : std::nullopt;
    const std::optional<float> longitude = projection != nullptr ? json_float(*projection, "origin_lon") : std::nullopt;
    if (!latitude || !longitude || std::abs(*latitude) > 90.0f || std::abs(*longitude) > 180.0f) {
        return {std::nullopt, "projection: not an origin_lat and origin_lon on Earth"};
    }
    world.latitude_deg = *latitude;
    world.longitude_deg = *longitude;

    parsed<height_grid> ground = ground_from_json(*root);
    if (!ground.value) {
        return {std::nullopt, ground.error};
    }
    world.ground = std::move(*ground.value);
    parsed<std::vector<course_world_cart_road>> roads = json_elements<course_world_cart_road>(*root, "cart_roads", cart_road_from_json);
    if (!roads.value) {
        return {std::nullopt, roads.error};
    }
    world.cart_roads = std::move(*roads.value);
    parsed<std::vector<course_world_fence>> fences = json_elements<course_world_fence>(*root, "fences", fence_from_json);
    if (!fences.value) {
        return {std::nullopt, fences.error};
    }
    world.fences = std::move(*fences.value);
    parsed<std::vector<course_world_collectible>> collectibles =
        json_elements<course_world_collectible>(*root, "collectibles", collectible_from_json);
    if (!collectibles.value) {
        return {std::nullopt, collectibles.error};
    }
    world.collectibles = std::move(*collectibles.value);
    return {std::move(world), ""};
}

parsed<course_world_definition> parse_course_world_from_text(const std::string& text, const course_definition& course) {
    return parse_world_from_text(text, &course);
}
