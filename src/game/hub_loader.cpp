#include "game/hub_definition.h"
#include "game/course_loader.h"
#include "game/json_util.h"

#include <algorithm>

std::optional<hub_definition> parse_hub_from_text(const std::string& text) {
    const auto root = parse_json(text);
    const auto environment = parse_place_from_text(text).value;
    if (!root || !environment || root->contains("holes") || root->contains("practice")) { return std::nullopt; }
    const auto arrival_door = json_object(*root, "arrival");
    const auto arrival = arrival_door ? json_vec3(*arrival_door, "position") : std::nullopt;
    const auto heading = arrival_door ? json_float(*arrival_door, "heading") : std::nullopt;
    const auto mats = json_array(*root, "tee_mats");
    const auto targets = json_array(*root, "targets");
    const auto cups = json_array(*root, "practice_cups");
    const auto anchors = json_object(*root, "hub");
    if (!arrival || !heading || !mats || mats->empty() || !targets || !cups || !anchors) { return std::nullopt; }
    hub_definition hub;
    static_cast<place_definition&>(hub) = *environment;
    hub.arrival = *arrival;
    hub.arrival_heading = *heading;
    const auto anchor = [&](const char* key) -> std::optional<hub_anchor> {
        const auto object = json_object(*anchors, key);
        const auto position = object ? json_vec3(*object, "position") : std::nullopt;
        const auto reach = object ? json_float(*object, "reach") : std::nullopt;
        return position && reach && *reach > 0 ? std::optional<hub_anchor>({*position, *reach}) : std::nullopt;
    };
    const auto book = anchor("book");
    if (!book) { return std::nullopt; }
    hub.anchors = {*book, {}, {}};
    const auto pads = json_array(*anchors, "pads");
    const auto props = json_array(*anchors, "props");
    const auto strips = json_array(*root, "tee_strips");
    const auto lights = json_array(*root, "floodlights");
    const auto ambience = json_array(*root, "ambience");
    if (!ambience) { return std::nullopt; }
    for (const auto& value : *ambience) {
        const auto sound = json_string(value, "sound");
        const auto position = json_vec3(value, "position");
        if (!sound || sound->empty() || !position) { return std::nullopt; }
        hub.ambience.push_back({*sound, *position});
    }
    if (!pads || pads->empty() || !props || !strips || strips->empty() || !lights) { return std::nullopt; }
    for (const auto& value : *props) {
        const auto model = json_string(value, "model");
        const auto position = json_vec3(value, "position");
        const auto facing = json_float(value, "heading");
        const auto height = json_float(value, "height");
        if (!model || model->empty() || !position || !facing || !height) { return std::nullopt; }
        hub.anchors.props.push_back({*model, *position, *facing, *height});
    }
    for (const auto& value : *strips) {
        const auto position = json_vec3(value, "position"), aim = json_vec3(value, "aim");
        const auto half_width = json_float(value, "half_width"), half_length = json_float(value, "half_length");
        if (!position || !aim || !half_width || !half_length || *half_width <= 0 || *half_length <= 0) { return std::nullopt; }
        hub.tee_strips.push_back({*position, *aim, *half_width, *half_length});
    }
    for (const auto& value : *lights) {
        const auto foot = json_vec3(value, "foot"), aim = json_vec3(value, "aim");
        if (!foot || !aim) { return std::nullopt; }
        hub.floodlights.push_back({*foot, *aim});
    }
    for (const auto& value : *pads) {
        const auto name = json_string(value, "name");
        const auto position = json_vec3(value, "position");
        const auto reach = json_float(value, "reach");
        if (!name || name->empty() || !position || !reach || *reach <= 0) { return std::nullopt; }
        const bool repeated = std::any_of(hub.anchors.pads.begin(), hub.anchors.pads.end(),
            [&](const hub_pad& pad) { return pad.name == *name; });
        if (repeated) { return std::nullopt; }
        hub.anchors.pads.push_back({*name, *position, *reach});
    }
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
        const auto board = json_vec3(value, "board");
        const auto distance = json_int(value, "distance_meters");
        if (!position || !radius || *radius <= 0 || !board || !distance || *distance <= 0) { return std::nullopt; }
        hub.targets.push_back({*position, *radius, *board, *distance});
    }
    const auto points = json_vec3_array(*cups);
    if (!points) { return std::nullopt; }
    hub.practice_cups = *points;
    hub.world = "world/hub/" + hub.world;
    hub.backdrop.sky = "world/hub/" + hub.backdrop.sky;
    hub.backdrop.land = "world/hub/" + hub.backdrop.land;
    return hub;
}
