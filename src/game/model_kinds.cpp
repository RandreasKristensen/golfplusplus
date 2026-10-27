#include "game/model_kinds.h"

const std::vector<model_kind>& model_kinds() {
    static const std::vector<model_kind> kinds{
        // Lit at its tagged ember, its smoke rising from its point.
        {"cigarette", {"ember_dim", "ember_bright", "smoke"}, {"smoke"}, {}, {"ember"}},
        // Beer poured from its opening.
        {"can", {"beer", "foam"}, {"pour"}, {}, {}},
        // Its wheels' hubs and its steering wheel's, which the code turns,
        // and where each seat's bag straps on.
        {"cart",
         {"trim", "rubber", "hub_cap"},
         {"wheel_left_rear", "wheel_right_rear", "wheel_left_front", "wheel_right_front", "steering_wheel"},
         {"driver_bag", "passenger_bag"},
         {}},
        // Held at its grip; its face is what it strikes with.
        {"club", {}, {"grip", "face"}, {}, {}},
        // Hung at a mount by its hang point.
        {"trinket", {}, {"hang"}, {}, {}},
        {"bag", {}, {"hang"}, {}, {}},
        // Fitted to the course by the code: colours only.
        {"tree", {"trunk", "leaves"}, {}, {}, {}},
        {"fence", {"post"}, {}, {}, {}},
        {"floodlight", {"pole", "head"}, {}, {}, {}},
        {"hole_sign", {"board", "post"}, {}, {}, {}},
    };
    return kinds;
}

const model_kind* find_model_kind(const std::string& name) {
    for (const model_kind& kind : model_kinds()) {
        if (name == kind.name) {
            return &kind;
        }
    }
    return nullptr;
}

const std::vector<required_model>& required_models() {
    static const std::vector<required_model> models{
        {tree_model, "tree"},
        {fence_model, "fence"},
        {floodlight_model, "floodlight"},
        {hole_sign_model, "hole_sign"},
    };
    return models;
}
