#include "game/hub_definition.h"
#include "game/course_loader.h"
#include "game/json_util.h"

std::optional<hub_definition> parse_hub_from_text(const std::string& text) {
    const auto root = parse_json(text);
    const auto environment = parse_place_from_text(text);
    if (!root || !environment || root->contains("holes") || root->contains("practice")) { return std::nullopt; }
    const auto arrival = json_vec3(*root, "arrival");
    const auto mats = json_array(*root, "tee_mats");
    const auto targets = json_array(*root, "targets");
    const auto cups = json_array(*root, "practice_cups");
    const auto anchors = json_object(*root, "hub");
    if (!arrival || !mats || mats->empty() || !targets || !cups || !anchors) { return std::nullopt; }
    hub_definition hub;
    static_cast<place_definition&>(hub) = *environment;
    hub.arrival = *arrival;
    const auto anchor = [&](const char* key) -> std::optional<hub_anchor> {
        const auto object = json_object(*anchors, key);
        const auto position = object ? json_vec3(*object, "position") : std::nullopt;
        const auto reach = object ? json_float(*object, "reach") : std::nullopt;
        return position && reach && *reach > 0 ? std::optional<hub_anchor>({*position, *reach}) : std::nullopt;
    };
    const auto book = anchor("book");
    if (!book) { return std::nullopt; }
    hub.anchors = {*book};
    for (const auto& value : *mats) {
        const auto position = json_vec3(value, "position"), aim = json_vec3(value, "aim");
        const auto reach = json_float(value, "reach");
        if (!position || !aim || !reach || *reach <= 0) { return std::nullopt; }
        const auto raised = json_bool(value, "tee_box");
        if (!raised) { return std::nullopt; }
        hub.tee_mats.push_back({*position, *aim, *reach, *raised});
    }
    for (const auto& value : *targets) {
        const auto position = json_vec3(value, "position");
        const auto radius = json_float(value, "radius");
        if (!position || !radius || *radius <= 0) { return std::nullopt; }
        hub.targets.push_back({*position, *radius});
    }
    const auto points = json_vec3_array(*cups);
    if (!points) { return std::nullopt; }
    hub.practice_cups = *points;
    hub.world = "world/hub/" + hub.world;
    hub.backdrop.sky = "world/hub/" + hub.backdrop.sky;
    hub.backdrop.land = "world/hub/" + hub.backdrop.land;
    return hub;
}
