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
};
struct hub_definition : place_definition {
    glm::vec3 arrival{0.0f};
    std::vector<hub_tee_mat> tee_mats;
    std::vector<hub_target> targets;
    std::vector<glm::vec3> practice_cups;
    hub_anchors anchors;
};
std::optional<hub_definition> parse_hub_from_text(const std::string& text);
std::optional<hub_definition> load_hub(const std::string& asset_root);
