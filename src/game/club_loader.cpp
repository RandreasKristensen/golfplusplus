#include "game/club_loader.h"

#include "game/json_util.h"

#include <algorithm>

std::optional<club_definition> parse_club_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    const json* stats = root ? json_object(*root, "stats") : nullptr;
    if (stats == nullptr) {
        return std::nullopt;
    }

    const std::optional<std::string> id = json_string(*root, "id");
    const std::optional<std::string> hit_sound = json_string(*root, "hit_sound");
    const std::optional<float> power = json_float(*stats, "power");
    const std::optional<float> loft_degrees = json_float(*stats, "loft_degrees");
    const std::optional<float> backspin = json_float(*stats, "backspin");
    const std::optional<float> side_spin = json_float(*stats, "side_spin");
    if (!id || !hit_sound || !power || !loft_degrees || !backspin || !side_spin) {
        return std::nullopt;
    }

    club_definition club;
    club.id = *id;
    club.name = json_string(*root, "name").value_or(club.id);
    club.label = json_string(*root, "label").value_or(club.name);
    club.hit_sound = *hit_sound;
    club.bag_order = json_int(*root, "bag_order").value_or(0);
    club.stats.power = *power;
    club.stats.loft_degrees = *loft_degrees;
    club.stats.backspin = *backspin;
    club.stats.side_spin = *side_spin;
    club.stats.timing_speed = json_float(*stats, "timing_speed").value_or(1.0f);
    club.stats.roll_friction_scale = json_float(*stats, "roll_friction_scale").value_or(1.0f);
    return club;
}

std::vector<club_definition> parse_clubs_from_texts(const std::vector<std::string>& texts) {
    std::vector<club_definition> clubs;
    for (const std::string& text : texts) {
        if (std::optional<club_definition> club = parse_club_from_text(text)) {
            clubs.push_back(std::move(*club));
        }
    }
    std::sort(clubs.begin(), clubs.end(), [](const club_definition& a, const club_definition& b) {
        return a.bag_order != b.bag_order ? a.bag_order < b.bag_order : a.id < b.id;
    });
    return clubs;
}
