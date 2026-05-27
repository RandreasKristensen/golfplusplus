#include "game/course_world_loader.h"

#include "game/progression.h"

#include <algorithm>
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

std::optional<float> float_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<float>();
}

std::optional<int> int_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    return it->get<int>();
}

std::optional<bool> bool_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return std::nullopt;
    }
    return it->get<bool>();
}

bool contains_string(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
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

std::optional<std::vector<glm::vec3>> polyline_from_json(const json& value) {
    if (!value.is_array() || value.size() < 2) {
        return std::nullopt;
    }

    std::vector<glm::vec3> points;
    points.reserve(value.size());
    for (const json& point : value) {
        const std::optional<glm::vec3> parsed = vec3_from_json(point);
        if (!parsed) {
            return std::nullopt;
        }
        points.push_back(*parsed);
    }
    return points;
}

std::optional<course_world_path> path_from_json(const json& value, const bool shortcut) {
    if (!value.is_object()) {
        return std::nullopt;
    }

    auto polyline_it = value.find("polyline");
    if (polyline_it == value.end()) {
        polyline_it = value.find("points");
    }
    if (polyline_it == value.end()) {
        return std::nullopt;
    }
    const std::optional<std::vector<glm::vec3>> polyline = polyline_from_json(*polyline_it);
    if (!polyline) {
        return std::nullopt;
    }

    course_world_path path;
    path.id = string_at(value, "id").value_or("");
    path.surface = string_at(value, "surface").value_or("");
    path.source = string_at(value, "source").value_or("");
    path.osm_ref = string_at(value, "osm_ref").value_or("");
    path.width = float_at(value, "width").value_or(shortcut ? 2.0f : 4.0f);
    path.required_skill_id = string_at(value, "required_skill_id").value_or("");
    path.required_level = int_at(value, "required_level").value_or(1);
    path.polyline = *polyline;
    return path;
}

std::optional<course_world_hole_start> hole_start_from_json(const json& value, const course_definition& course) {
    if (!value.is_object()) {
        return std::nullopt;
    }

    const std::optional<int> hole_index = int_at(value, "hole_index");
    if (!hole_index || *hole_index < 0 || *hole_index >= static_cast<int>(course.holes.size())) {
        return std::nullopt;
    }

    const auto position_it = value.find("position");
    const auto return_it = value.find("return_position");
    if (position_it == value.end() || return_it == value.end()) {
        return std::nullopt;
    }
    const std::optional<glm::vec3> position = vec3_from_json(*position_it);
    const std::optional<glm::vec3> return_position = vec3_from_json(*return_it);
    if (!position || !return_position) {
        return std::nullopt;
    }

    course_world_hole_start start;
    start.id = string_at(value, "id").value_or("");
    start.hole_index = *hole_index;
    start.position = *position;
    start.return_position = *return_position;
    start.interaction_radius = float_at(value, "interaction_radius").value_or(4.0f);
    return start;
}

std::vector<course_world_path> paths_from_json(const json& root, const char* key, const bool shortcut) {
    std::vector<course_world_path> paths;
    const auto it = root.find(key);
    if (it == root.end() || !it->is_array()) {
        return paths;
    }

    for (const json& value : *it) {
        const std::optional<course_world_path> path = path_from_json(value, shortcut);
        if (path) {
            paths.push_back(*path);
        }
    }
    return paths;
}

void append_spawn_zones(course_world_definition& world, const json& root) {
    const auto it = root.find("spawn_zones");
    if (it == root.end() || !it->is_array()) {
        return;
    }
    for (const json& value : *it) {
        if (!value.is_object()) {
            continue;
        }
        course_world_spawn_zone zone;
        zone.id = string_at(value, "id").value_or("");
        zone.kind = string_at(value, "kind").value_or("");
        zone.near = string_at(value, "near").value_or("");
        zone.count = int_at(value, "count").value_or(0);
        world.spawn_zones.push_back(zone);
    }
}

void append_interactables(course_world_definition& world, const json& root) {
    const auto it = root.find("interactables");
    if (it == root.end() || !it->is_array()) {
        return;
    }
    for (const json& value : *it) {
        if (!value.is_object()) {
            continue;
        }
        const auto position_it = value.find("position");
        if (position_it == value.end()) {
            continue;
        }
        const std::optional<glm::vec3> position = vec3_from_json(*position_it);
        if (!position) {
            continue;
        }
        course_world_interactable interactable;
        interactable.id = string_at(value, "id").value_or("");
        interactable.kind = string_at(value, "kind").value_or("");
        interactable.content_id = string_at(value, "content_id").value_or("");
        interactable.position = *position;
        interactable.interaction_radius = float_at(value, "interaction_radius").value_or(3.0f);
        world.interactables.push_back(interactable);
    }
}

std::vector<course_world_skill_reward> skill_rewards_from_json(const json& value) {
    std::vector<course_world_skill_reward> rewards;
    if (!value.is_object()) {
        return rewards;
    }

    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!it.value().is_number_integer()) {
            continue;
        }
        course_world_skill_reward reward;
        reward.skill_id = it.key();
        reward.xp = std::max(0, it.value().get<int>());
        if (!reward.skill_id.empty() && reward.xp > 0) {
            rewards.push_back(reward);
        }
    }
    return rewards;
}

std::optional<course_world_collectible> collectible_from_json(const json& value) {
    if (!value.is_object()) {
        return std::nullopt;
    }

    const std::string id = string_at(value, "id").value_or("");
    const auto position_it = value.find("position");
    if (id.empty() || position_it == value.end()) {
        return std::nullopt;
    }
    const std::optional<glm::vec3> position = vec3_from_json(*position_it);
    if (!position) {
        return std::nullopt;
    }

    course_world_collectible collectible;
    collectible.id = id;
    collectible.kind = string_at(value, "kind").value_or("collectible");
    collectible.position = *position;
    collectible.interaction_radius = float_at(value, "interaction_radius").value_or(2.5f);
    collectible.repeatable = bool_at(value, "repeatable").value_or(false);
    collectible.repeatable_cooldown_holes = std::max(0, int_at(value, "repeatable_cooldown_holes").value_or(0));

    const auto reward_it = value.find("reward");
    if (reward_it != value.end() && reward_it->is_object()) {
        collectible.money = std::max(0, int_at(*reward_it, "money").value_or(0));
        collectible.unlock_id = string_at(*reward_it, "unlock_id").value_or("");
        collectible.world_flag = string_at(*reward_it, "world_flag").value_or("");
        const auto skills_it = reward_it->find("skill_xp");
        if (skills_it != reward_it->end()) {
            collectible.skill_rewards = skill_rewards_from_json(*skills_it);
        }
    }

    const auto requirement_it = value.find("requirement");
    if (requirement_it != value.end() && requirement_it->is_object()) {
        collectible.requirement.skill_id = string_at(*requirement_it, "skill_id").value_or("");
        collectible.requirement.min_level = std::max(1, int_at(*requirement_it, "min_level").value_or(1));
        collectible.requirement.required_world_flag = string_at(*requirement_it, "required_world_flag").value_or("");
        collectible.requirement.required_completed_course_id = string_at(*requirement_it, "required_completed_course_id").value_or("");
    }

    return collectible;
}

void append_collectibles(course_world_definition& world, const json& root) {
    const auto it = root.find("collectibles");
    if (it == root.end() || !it->is_array()) {
        return;
    }
    for (const json& value : *it) {
        const std::optional<course_world_collectible> collectible = collectible_from_json(value);
        if (collectible) {
            world.collectibles.push_back(*collectible);
        }
    }
}

std::optional<course_world_definition> world_from_json(const json& root, const course_definition& course) {
    if (!root.is_object()) {
        return std::nullopt;
    }

    course_world_definition world;
    world.id = string_at(root, "id").value_or("");
    world.name = string_at(root, "name").value_or(world.id);

    const auto projection_it = root.find("projection");
    if (projection_it != root.end() && projection_it->is_object()) {
        world.origin_lat = projection_it->value("origin_lat", 0.0);
        world.origin_lon = projection_it->value("origin_lon", 0.0);
    }
    const auto origin_it = root.find("origin");
    if (origin_it != root.end() && origin_it->is_object()) {
        world.origin_lat = origin_it->value("lat", world.origin_lat);
        world.origin_lon = origin_it->value("lon", world.origin_lon);
    }

    const auto spawn_it = root.find("spawn");
    std::optional<glm::vec3> spawn_position;
    if (spawn_it != root.end() && spawn_it->is_object()) {
        const auto spawn_position_it = spawn_it->find("position");
        if (spawn_position_it != spawn_it->end()) {
            spawn_position = vec3_from_json(*spawn_position_it);
        }
        world.spawn.id = string_at(*spawn_it, "id").value_or("spawn");
        world.spawn.radius = float_at(*spawn_it, "radius").value_or(5.0f);
    } else {
        const auto clubhouse_it = root.find("clubhouse_spawn");
        if (clubhouse_it != root.end()) {
            spawn_position = vec3_from_json(*clubhouse_it);
        }
        world.spawn.id = "clubhouse_spawn";
        world.spawn.radius = 5.0f;
    }
    if (!spawn_position) {
        return std::nullopt;
    }
    world.spawn.position = *spawn_position;

    const auto starts_it = root.find("hole_starts");
    if (starts_it == root.end() || !starts_it->is_array()) {
        return std::nullopt;
    }
    for (const json& value : *starts_it) {
        const std::optional<course_world_hole_start> start = hole_start_from_json(value, course);
        if (!start) {
            return std::nullopt;
        }
        world.hole_starts.push_back(*start);
    }
    if (world.hole_starts.empty()) {
        return std::nullopt;
    }

    world.cart_roads = paths_from_json(root, "cart_roads", false);
    world.walking_shortcuts = paths_from_json(root, "walking_shortcuts", true);
    append_collectibles(world, root);
    append_spawn_zones(world, root);
    append_interactables(world, root);
    return world;
}
}

std::optional<course_world_definition> load_course_world_from_file(const std::string& path,
                                                                   const course_definition& course) {
    const std::optional<json> root = load_json_file(path);
    if (!root) {
        return std::nullopt;
    }
    return world_from_json(*root, course);
}

std::string course_world_file_path(const std::string& asset_root, const course_definition& course) {
    if (course.world.empty()) {
        return {};
    }

    const std::filesystem::path reference(course.world);
    if (reference.is_absolute()) {
        return reference.string();
    }
    return (std::filesystem::path(asset_root) / reference).string();
}

bool shortcut_unlocked(const save_data& save, const course_world_path& shortcut) {
    if (shortcut.required_skill_id.empty()) {
        return true;
    }
    return skill_level(skill_xp(save.skills, shortcut.required_skill_id)) >= shortcut.required_level;
}

bool collectible_available(const save_data& save,
                           const course_world_collectible& collectible,
                           const int current_hole_index) {
    const course_world_collectible_requirement& requirement = collectible.requirement;
    if (!requirement.skill_id.empty() &&
        skill_level(skill_xp(save.skills, requirement.skill_id)) < std::max(1, requirement.min_level)) {
        return false;
    }
    if (!requirement.required_world_flag.empty() &&
        !contains_string(save.world_flags, requirement.required_world_flag)) {
        return false;
    }
    if (!requirement.required_completed_course_id.empty() &&
        !contains_string(save.completed_course_ids, requirement.required_completed_course_id)) {
        return false;
    }

    if (!collectible.repeatable) {
        return !contains_string(save.collected_ids, collectible.id);
    }

    const auto it = save.repeatable_collectibles.find(collectible.id);
    if (it == save.repeatable_collectibles.end()) {
        return true;
    }
    if (collectible.repeatable_cooldown_holes <= 0) {
        return true;
    }
    return current_hole_index - it->second.last_claimed_hole_index >= collectible.repeatable_cooldown_holes;
}

bool apply_collectible_reward(save_data& save,
                              const course_world_collectible& collectible,
                              const int current_hole_index) {
    if (!collectible_available(save, collectible, current_hole_index)) {
        return false;
    }

    save.money = std::max(0, save.money + std::max(0, collectible.money));
    for (const course_world_skill_reward& reward : collectible.skill_rewards) {
        add_skill_xp(save.skills, reward.skill_id, reward.xp);
    }
    if (!collectible.unlock_id.empty() && !contains_string(save.unlocked_items, collectible.unlock_id)) {
        save.unlocked_items.push_back(collectible.unlock_id);
    }
    if (!collectible.world_flag.empty() && !contains_string(save.world_flags, collectible.world_flag)) {
        save.world_flags.push_back(collectible.world_flag);
    }

    if (collectible.repeatable) {
        repeatable_collectible_state& state = save.repeatable_collectibles[collectible.id];
        state.claim_count = std::max(0, state.claim_count) + 1;
        state.last_claimed_hole_index = std::max(0, current_hole_index);
    } else if (!contains_string(save.collected_ids, collectible.id)) {
        save.collected_ids.push_back(collectible.id);
    }
    return true;
}
