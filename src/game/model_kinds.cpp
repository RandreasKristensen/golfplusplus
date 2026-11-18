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
        {"club", {}, {"grip", "face", "sole"}, {}, {}},
        // Hung at a mount by its hang point.
        {"trinket", {}, {"hang"}, {}, {}},
        // Its mouth the top its clubs come out of.
        {"bag", {}, {"hang", "mouth"}, {}, {}},
        // Fitted to the course by the code: colours only.
        {"fence", {"post"}, {}, {}, {}},
        {"floodlight", {"pole", "head"}, {}, {}, {}},
        {"hole_sign", {"board", "post"}, {}, {}, {}},
        // Board points frame the label the renderer writes on it.
        {"hub_book", {}, {"approach", "body_min", "body_max", "board_center", "board_size", "board_normal"}, {}, {}},
        // Built things placed from hub.json: a box of collision, and a board for
        // those that say something; a label is a board on a wall, nothing solid.
        {"hub_fixture", {}, {"body_min", "body_max"}, {}, {}},
        // A fixture whose speaker the starter's PA call is heard from.
        {"hub_announcer", {}, {"body_min", "body_max", "speaker"}, {}, {}},
        {"hub_sign", {}, {"body_min", "body_max", "board_center", "board_size", "board_normal"}, {}, {}},
        {"hub_label", {}, {"board_center", "board_size", "board_normal"}, {}, {}},
        {"hub_bus", {}, {"approach", "door", "body_min", "body_max", "board_center", "board_size", "board_normal",
                         "engine", "horn", "door_hinge", "wheel_contact_fl", "wheel_contact_fr", "wheel_contact_rl", "wheel_contact_rr"}, {}, {"door"}},
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

const std::vector<hub_model>& hub_models() {
    static const std::vector<hub_model> models{
        {"tee_book", "hub_book"}, {"hub_bus", "hub_bus"},
        {"hub_path_lamp", "hub_fixture"}, {"hub_clubhouse", "hub_fixture"}, {"hub_starter_hut", "hub_fixture"}, {"hub_pa_pole", "hub_announcer"},
        {"hub_bin", "hub_fixture"}, {"hub_bench", "hub_fixture"}, {"hub_terrace_table", "hub_fixture"},
        {"hub_flagpole", "hub_fixture"}, {"hub_range_divider", "hub_fixture"}, {"hub_ball_bucket", "hub_fixture"},
        {"hub_bag_stand", "hub_fixture"}, {"hub_ball_dispenser", "hub_fixture"},
        {"hub_car_red", "hub_fixture"}, {"hub_car_blue", "hub_fixture"}, {"hub_car_cream", "hub_fixture"}, {"hub_car_silver", "hub_fixture"},
        {clock_model, "hub_label"}, {departure_row_model, "hub_label"},
        {distance_board_model, "hub_sign"}, {departure_board_model, "hub_sign"},
    };
    return models;
}

const std::vector<required_model>& required_models() {
    static const std::vector<required_model> models{
        {fence_model, "fence"},
        {floodlight_model, "floodlight"},
        {hole_sign_model, "hole_sign"},
    };
    return models;
}
