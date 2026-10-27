#include "game/item_catalog.h"

#include "game/json_util.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string_view>

namespace {
constexpr item_kind all_kinds[] = {item_kind::smokable, item_kind::drinkable, item_kind::cart, item_kind::trinket,
                                   item_kind::club,     item_kind::bag};
const std::vector<std::string> merged_keys{"use", "spec"};
constexpr const char* club_classes[] = {"driver", "woods", "hybrids", "long_irons", "short_irons", "wedges", "putter"};
constexpr double milliseconds_per_second = 1000.0;

std::int64_t milliseconds(const float seconds) {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(seconds) * milliseconds_per_second));
}

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

std::optional<item_effect> parse_effect(const json& object) {
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

std::optional<item_use_rules> parse_use(const json& object) {
    const std::optional<float> seconds = json_float(object, "seconds_per_use");
    const std::optional<std::string> sound = json_string(object, "sound");
    if (!seconds || !(*seconds > 0.0f) || !sound || sound->empty()) {
        return std::nullopt;
    }
    item_use_rules use;
    use.seconds_per_use = *seconds;
    use.sound = *sound;
    if (const json* xp = json_object(object, "xp_per_use")) {
        const std::optional<std::string> skill = json_string(*xp, "skill");
        const std::optional<int> amount = json_int(*xp, "xp");
        if (!skill || skill->empty() || !amount || *amount <= 0) {
            return std::nullopt;
        }
        use.xp_per_use = xp_reward{*skill, *amount};
    } else if (object.contains("xp_per_use")) {
        return std::nullopt;
    }
    return use;
}

std::optional<club_spec> parse_club_spec(const json& object) {
    struct field {
        const char* key;
        float club_spec::*member;
        float low;
        float high;
    };
    static constexpr field fields[] = {
        {"static_loft_degrees", &club_spec::static_loft_degrees, 0.0f, 70.0f},
        {"lie_degrees", &club_spec::lie_degrees, 45.0f, 90.0f},
        {"length_meters", &club_spec::length_meters, 0.5f, 1.3f},
        {"effective_mass_kg", &club_spec::effective_mass_kg, 0.1f, 1.0f},
        {"face_cor", &club_spec::face_cor, 0.0f, 0.83f},
        {"face_grip", &club_spec::face_grip, 0.0f, 1.0f},
        {"face_friction", &club_spec::face_friction, 0.05f, 1.0f},
        {"face_share", &club_spec::face_share, 0.0f, 1.0f},
        {"spin_scale", &club_spec::spin_scale, 0.1f, 2.0f},
        {"gear_effect", &club_spec::gear_effect, 0.0f, 50.0f},
        {"face_centre_height_meters", &club_spec::face_centre_height_meters, 0.005f, 0.06f},
        {"delivered_loft_degrees", &club_spec::delivered_loft_degrees, -5.0f, 70.0f},
        {"speed_at_99", &club_spec::speed_at_99, 0.5f, 80.0f},
        {"stock_attack_degrees", &club_spec::stock_attack_degrees, -15.0f, 15.0f},
        {"stock_ball_position", &club_spec::stock_ball_position, -1.0f, 1.0f},
        {"stock_backswing", &club_spec::stock_backswing, 0.0f, 1.0f},
        {"sole_bounce_degrees", &club_spec::sole_bounce_degrees, 0.0f, 20.0f},
        {"forgiveness", &club_spec::forgiveness, 0.0f, 1.0f},
        {"max_face_open_degrees", &club_spec::max_face_open_degrees, 0.0f, 60.0f},
        {"max_face_closed_degrees", &club_spec::max_face_closed_degrees, 0.0f, 60.0f},
        {"sand_face_open_degrees", &club_spec::sand_face_open_degrees, 0.0f, 60.0f},
    };
    club_spec spec;
    for (const field& each : fields) {
        const std::optional<float> value = json_float(object, each.key);
        if (!value || *value < each.low || *value > each.high) {
            return std::nullopt;
        }
        spec.*each.member = *value;
    }
    return spec;
}

std::optional<club_definition> parse_club(const json& root) {
    const json* spec_object = json_object(root, "spec");
    const std::optional<club_spec> spec = spec_object != nullptr ? parse_club_spec(*spec_object) : std::nullopt;
    const std::optional<std::string> label = json_string(root, "label");
    const std::optional<std::string> club_class = json_string(root, "class");
    const std::optional<std::string> hit_sound = json_string(root, "hit_sound");
    const std::optional<std::string> mishit_sound = json_string(root, "mishit_sound");
    const std::optional<int> bag_order = json_int(root, "bag_order");
    const bool known_class = club_class && std::any_of(std::begin(club_classes), std::end(club_classes),
                                                       [&club_class](const char* name) { return *club_class == name; });
    if (!spec || !label || label->empty() || !known_class || !hit_sound || hit_sound->empty() || !mishit_sound ||
        mishit_sound->empty() || !bag_order) {
        return std::nullopt;
    }
    club_definition club;
    club.label = *label;
    club.club_class = *club_class;
    club.hit_sound = *hit_sound;
    club.mishit_sound = *mishit_sound;
    club.bag_order = *bag_order;
    club.spec = *spec;
    return club;
}

std::optional<cart_spec> parse_cart_spec(const json& object) {
    struct field {
        const char* key;
        float cart_spec::*member;
        float low;
        float high;
    };
    static constexpr field fields[] = {
        {"mass_kg", &cart_spec::mass_kg, 100.0f, 1500.0f},
        {"motor_power_w", &cart_spec::motor_power_w, 500.0f, 20000.0f},
        {"max_drive_force_n", &cart_spec::max_drive_force_n, 100.0f, 10000.0f},
        {"reverse_share", &cart_spec::reverse_share, 0.1f, 1.0f},
        {"top_speed_mps", &cart_spec::top_speed_mps, 1.0f, 20.0f},
        {"wheelbase_m", &cart_spec::wheelbase_m, 0.5f, 4.0f},
        {"track_m", &cart_spec::track_m, 0.5f, 2.0f},
        {"cg_height_m", &cart_spec::cg_height_m, 0.2f, 1.5f},
        {"max_steer_degrees", &cart_spec::max_steer_degrees, 5.0f, 60.0f},
        {"tip_seconds", &cart_spec::tip_seconds, 0.0f, 5.0f},
        {"wade_depth_m", &cart_spec::wade_depth_m, 0.05f, 2.0f},
        {"right_seconds", &cart_spec::right_seconds, 0.1f, 60.0f},
        {"tow_seconds", &cart_spec::tow_seconds, 1.0f, 3600.0f},
    };
    cart_spec spec;
    for (const field& each : fields) {
        const std::optional<float> value = json_float(object, each.key);
        if (!value || *value < each.low || *value > each.high) {
            return std::nullopt;
        }
        spec.*each.member = *value;
    }
    return spec;
}

std::optional<item_definition> parse_resolved_item(const std::string& id, const item_kind kind, const json& root) {
    const std::optional<std::string> model = json_string(root, "model");
    const std::optional<std::string> name = json_string(root, "name");
    const std::optional<std::string> stacking = json_string(root, "stacking");
    const json* effects = json_array(root, "effects");
    if (!model || model->empty() || !name || name->empty() || !stacking || (*stacking != "refresh" && *stacking != "stack") ||
        effects == nullptr) {
        return std::nullopt;
    }
    item_definition item;
    item.id = id;
    item.kind = kind;
    item.model = *model;
    item.name = *name;
    item.stacking = *stacking == "refresh" ? item_stacking::refresh : item_stacking::stack;
    if (const json* use = json_object(root, "use")) {
        item.use = parse_use(*use);
        if (!item.use) {
            return std::nullopt;
        }
    }
    const bool used = kind == item_kind::smokable || kind == item_kind::drinkable;
    if (used != item.use.has_value()) {
        return std::nullopt;
    }
    if (kind == item_kind::club) {
        item.club = parse_club(root);
        if (!item.club) {
            return std::nullopt;
        }
        item.club->id = id;
        item.club->name = item.name;
        item.club->model = item.model;
    }
    if (kind == item_kind::cart) {
        const json* spec = json_object(root, "spec");
        item.cart = spec != nullptr ? parse_cart_spec(*spec) : std::nullopt;
        if (!item.cart) {
            return std::nullopt;
        }
    }
    for (const json& object : *effects) {
        std::optional<item_effect> effect = object.is_object() ? parse_effect(object) : std::nullopt;
        if (!effect || (effect->duration == effect_duration::use && !item.use)) {
            return std::nullopt;
        }
        item.effects.push_back(std::move(*effect));
    }
    return item;
}
}

const char* item_kind_name(const item_kind kind) {
    switch (kind) {
    case item_kind::smokable:
        return "smokable";
    case item_kind::drinkable:
        return "drinkable";
    case item_kind::cart:
        return "cart";
    case item_kind::trinket:
        return "trinket";
    case item_kind::club:
        return "club";
    case item_kind::bag:
        return "bag";
    }
    return "";
}

std::optional<item_kind> item_kind_from_name(const std::string& name) {
    for (const item_kind kind : all_kinds) {
        if (name == item_kind_name(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

const item_definition* item_catalog::find(const std::string& id) const {
    const auto found = items.find(id);
    return found != items.end() ? &found->second : nullptr;
}

std::optional<item_definition> parse_item(const std::string& id, const item_kind kind, const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object() || root->contains("base")) {
        return std::nullopt;
    }
    return parse_resolved_item(id, kind, *root);
}

item_catalog_load_result parse_item_catalog(const std::map<std::string, std::string>& texts) {
    item_catalog_load_result result;
    // By kind, each kind's files by id: a base is of the item's own kind.
    std::map<item_kind, std::map<std::string, json>> files;
    constexpr std::string_view extension = ".json";
    for (const auto& [path, text] : texts) {
        // "<kind>/<id>.json", of a kind there is.
        std::optional<json> root = parse_json(text);
        const bool json_file = path.size() > extension.size() &&
            path.compare(path.size() - extension.size(), extension.size(), extension) == 0;
        bool placed = false;
        for (const item_kind kind : all_kinds) {
            const std::string folder = std::string(item_kind_name(kind)) + "/";
            const std::size_t id_size = path.size() - folder.size() - extension.size();
            if (root && json_file && path.size() > folder.size() + extension.size() && path.compare(0, folder.size(), folder) == 0 &&
                path.find('/', folder.size()) == std::string::npos) {
                files[kind][path.substr(folder.size(), id_size)] = std::move(*root);
                placed = true;
                break;
            }
        }
        if (!placed) {
            result.error = std::string("cannot load ") + items_path + "/" + path;
            return result;
        }
    }
    item_catalog catalog;
    for (const auto& [kind, by_id] : files) {
        for (const auto& [id, root] : by_id) {
            const std::optional<json> resolved = json_with_base(id, by_id, merged_keys);
            std::optional<item_definition> item = resolved ? parse_resolved_item(id, kind, *resolved) : std::nullopt;
            if (!item || catalog.items.count(id) != 0) {
                result.error = std::string("cannot load ") + items_path + "/" + item_kind_name(kind) + "/" + id + ".json";
                return result;
            }
            catalog.items[id] = std::move(*item);
        }
    }
    result.catalog = std::move(catalog);
    return result;
}

std::vector<club_definition> clubs_of(const item_catalog& items) {
    std::vector<club_definition> clubs;
    for (const auto& [id, item] : items.items) {
        if (item.club) {
            clubs.push_back(*item.club);
        }
    }
    std::stable_sort(clubs.begin(), clubs.end(),
                     [](const club_definition& a, const club_definition& b) { return a.bag_order < b.bag_order; });
    return clubs;
}

std::vector<active_effect> effects_after_use(std::vector<active_effect> running, const item_definition& item,
                                             const std::int64_t now_ms) {
    if (item.stacking == item_stacking::refresh) {
        running.erase(std::remove_if(running.begin(), running.end(),
                                     [&item](const active_effect& effect) {
                                         return effect.item == item.id && effect.effect.duration != effect_duration::equipped;
                                     }),
                      running.end());
    }
    for (const item_effect& effect : item.effects) {
        std::int64_t ends_at = now_ms;
        if (effect.duration == effect_duration::seconds) {
            ends_at = now_ms + milliseconds(effect.seconds);
        } else if (effect.duration == effect_duration::use && item.use) {
            ends_at = now_ms + milliseconds(item.use->seconds_per_use);
        } else {
            continue;  // instant: nothing lasts; equipped: the loadout's
        }
        running.push_back(active_effect{item.id, effect, now_ms, ends_at});
    }
    return running;
}

bool item_use_playing(const item_definition& item, const std::int64_t used_at_ms, const std::int64_t now_ms) {
    return item.use && used_at_ms != 0 && now_ms >= used_at_ms && now_ms < used_at_ms + milliseconds(item.use->seconds_per_use);
}
