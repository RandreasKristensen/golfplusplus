#pragma once

// A hub has practice facilities and an arrival, never holes or a scorecard.
#include "game/place_definition.h"
#include "game/course_world_definition.h"
#include <optional>

inline constexpr const char* hub_manifest_path = "world/hub/hub.json";
struct hub_tee_mat {
    glm::vec3 position{0.0f};
    glm::vec3 aim{0.0f};
    float reach = 0.0f;
    bool raised = false;
};
struct hub_target {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
    glm::vec3 board{0.0f};  // where its distance board stands
    int distance_meters = 0;  // what the board says: from the nearest bay of the tee line that fires at it
};
// One levelled strip of the hitting line: the tee box every bay on it stands
// on, as a tee box is for a hole (game/tee_box.h).
struct hub_tee_strip {
    glm::vec3 position{0.0f};  // the middle of its top
    glm::vec3 aim{0.0f};       // the way it faces
    float half_width = 0.0f;   // along the line of bays
    float half_length = 0.0f;  // along the shots
};
// A floodlight pole by its foot, its lamp aimed at `aim` (game/floodlights.h).
struct hub_floodlight {
    glm::vec3 foot{0.0f};
    glm::vec3 aim{0.0f};
};
// A looping sound standing in the hub (birds in a shelterbelt, the terrace's
// chatter): a sound of assets/sound/sounds.json heard from `position`.
struct hub_ambience {
    std::string sound;
    glm::vec3 position{0.0f};
};
struct hub_definition : place_definition {
    glm::vec3 arrival{0.0f};   // the bus door, where every arrival stands
    float arrival_heading = 0.0f;  // radians, yaw_direction: zero faces +Z
    std::vector<hub_tee_mat> tee_mats;
    std::vector<hub_tee_strip> tee_strips;
    std::vector<hub_floodlight> floodlights;
    std::vector<hub_target> targets;
    std::vector<glm::vec3> practice_cups;
    std::vector<hub_ambience> ambience;
    hub_anchors anchors;
};
std::optional<hub_definition> parse_hub_from_text(const std::string& text);
std::optional<hub_definition> load_hub(const std::string& asset_root);
