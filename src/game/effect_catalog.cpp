#include "game/effect_catalog.h"

#include <algorithm>
#include <string_view>

namespace {
std::optional<effect_bucket> bucket_from_name(const std::string& name) {
    if (name == "flat") {
        return effect_bucket::flat;
    }
    if (name == "increased") {
        return effect_bucket::increased;
    }
    if (name == "more") {
        return effect_bucket::more;
    }
    return std::nullopt;
}

bool unit_colour(const glm::vec3& colour) {
    return colour.x >= 0.0f && colour.x <= 1.0f && colour.y >= 0.0f && colour.y <= 1.0f && colour.z >= 0.0f &&
           colour.z <= 1.0f;
}

std::optional<screen_vignette> parse_vignette(const json& object) {
    const std::optional<glm::vec3> colour = json_vec3(object, "colour");
    const std::optional<float> seconds = json_float(object, "seconds");
    if (!colour || !unit_colour(*colour) || !seconds || !(*seconds > 0.0f)) {
        return std::nullopt;
    }
    return screen_vignette{*colour, *seconds};
}

bool has_text(const std::optional<std::string>& value) {
    return value && !value->empty();
}
}

const event_effect_definition* effect_catalog::find(const std::string& id) const {
    const auto found = effects.find(id);
    return found != effects.end() ? &found->second : nullptr;
}

std::optional<item_effect> parse_effect_entry(const json& object) {
    const std::optional<std::string> target = json_string(object, "target");
    const std::optional<std::string> bucket_name = json_string(object, "bucket");
    const std::optional<effect_bucket> bucket = bucket_name ? bucket_from_name(*bucket_name) : std::nullopt;
    const std::optional<float> value = json_float(object, "value");
    if (!target || target->empty() || !bucket || !value || !object.contains("duration")) {
        return std::nullopt;
    }
    item_effect effect;
    effect.target = *target;
    effect.bucket = *bucket;
    effect.value = *value;
    const json& duration = object["duration"];
    if (duration.is_object()) {
        const std::optional<float> seconds = json_float(duration, "seconds");
        if (!seconds || !(*seconds > 0.0f)) {
            return std::nullopt;
        }
        effect.duration = effect_duration::seconds;
        effect.seconds = *seconds;
        return effect;
    }
    const std::string name = duration.is_string() ? duration.get<std::string>() : "";
    if (name == "instant") {
        effect.duration = effect_duration::instant;
    } else if (name == "use") {
        effect.duration = effect_duration::use;
    } else if (name == "equipped") {
        effect.duration = effect_duration::equipped;
    } else {
        return std::nullopt;
    }
    return effect;
}

std::optional<event_effect_definition> parse_event_effect(const std::string& id, const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }
    const std::optional<std::string> name = json_string(*root, "name");
    const std::optional<std::string> description = json_string(*root, "description");
    const std::optional<std::string> icon = json_string(*root, "icon");
    const std::optional<bool> harmful = json_bool(*root, "harmful");
    const json* entries = json_array(*root, "effects");
    if (!has_text(name) || !has_text(description) || !has_text(icon) || !harmful || entries == nullptr || entries->empty()) {
        return std::nullopt;
    }
    event_effect_definition effect;
    effect.id = id;
    effect.name = *name;
    effect.description = *description;
    effect.icon = *icon;
    effect.harmful = *harmful;
    for (const json& object : *entries) {
        std::optional<item_effect> entry = object.is_object() ? parse_effect_entry(object) : std::nullopt;
        if (!entry || entry->duration != effect_duration::seconds) {
            return std::nullopt;
        }
        effect.effects.push_back(std::move(*entry));
    }
    if (root->contains("screen")) {
        const json* screen = json_object(*root, "screen");
        const json* vignette = screen != nullptr ? json_object(*screen, "vignette") : nullptr;
        if (vignette == nullptr) {
            return std::nullopt;
        }
        effect.vignette = parse_vignette(*vignette);
        if (!effect.vignette) {
            return std::nullopt;
        }
    }
    return effect;
}

effect_catalog_load_result parse_effect_catalog(const std::map<std::string, std::string>& texts) {
    effect_catalog_load_result result;
    constexpr std::string_view extension = ".json";
    effect_catalog catalog;
    for (const auto& [path, text] : texts) {
        const bool plain_json_file = path.size() > extension.size() &&
            path.compare(path.size() - extension.size(), extension.size(), extension) == 0 &&
            path.find('/') == std::string::npos;
        const std::string id = plain_json_file ? path.substr(0, path.size() - extension.size()) : std::string();
        std::optional<event_effect_definition> effect = plain_json_file ? parse_event_effect(id, text) : std::nullopt;
        if (!effect) {
            result.error = std::string("cannot load ") + effects_path + "/" + path;
            return result;
        }
        catalog.effects[id] = std::move(*effect);
    }
    if (catalog.effects.empty()) {
        result.error = std::string("no effects in ") + effects_path;
        return result;
    }
    result.catalog = std::move(catalog);
    return result;
}

std::vector<active_effect> effects_after_event(std::vector<active_effect> running, const event_effect_definition& effect,
                                               const std::int64_t now_ms) {
    const effect_source source = event_source(effect.id);
    // The event's earlier effects end where the new one starts: those not yet
    // begun by then go, so a refresh at a future start leaves the time up to
    // it as it was.
    for (active_effect& each : running) {
        if (each.source == source) {
            each.ends_at_ms = std::min(each.ends_at_ms, now_ms);
        }
    }
    running.erase(std::remove_if(running.begin(), running.end(),
                                 [&source](const active_effect& each) {
                                     return each.source == source && each.ends_at_ms <= each.started_at_ms;
                                 }),
                  running.end());
    for (const item_effect& entry : effect.effects) {
        running.push_back(active_effect{source, entry, now_ms, now_ms + effect_milliseconds(entry.seconds)});
    }
    return running;
}

std::optional<std::int64_t> event_started_at(const std::vector<active_effect>& effects, const std::string& event_id,
                                             const std::int64_t now_ms) {
    const effect_source source = event_source(event_id);
    std::optional<std::int64_t> started;
    for (const active_effect& each : effects) {
        if (each.source == source && effect_running(each, now_ms)) {
            started = started ? std::min(*started, each.started_at_ms) : each.started_at_ms;
        }
    }
    return started;
}
