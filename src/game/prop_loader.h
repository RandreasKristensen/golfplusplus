#pragma once

// The things drawn in the world, one file each in assets/world/props/: its
// colours by name, and for a thing of fixed shape the parts it is built
// from in its own frame (x left, y up, z forward), plus named points the
// code animates from (where a cigarette's smoke rises). What is fitted to
// the course (a tree's size, a fence's line, a floodlight's pole) or moves
// (a cart's wheels) is the code's; its file holds its colours. GL-free.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// The folder of prop files under the asset root.
inline constexpr const char* props_path = "world/props";

enum class prop_shape {
    cylinder,  // from `from` to `to`, `radius` round
    sphere,    // `radius` round `at`
    box,       // `size` (whole extents) round `at`, turned by `turn_degrees`
    panel      // a flat rectangle, `size` x and y, round `at`, facing z, turned by `turn_degrees`
};

struct prop_part {
    prop_shape shape = prop_shape::sphere;
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    glm::vec3 at{0.0f};
    glm::vec3 size{0.0f};
    // Pitch (x), yaw (y) and roll (z), as camera_local_model turns.
    glm::vec3 turn_degrees{0.0f};
    float radius = 0.0f;
    glm::vec3 color{0.0f};
    // The glow floor it shows at least (lighting.json's glow), empty for none.
    std::string glow;
    // What the code does with it ("ember": flickers), empty for nothing.
    std::string tag;
};

struct prop_model {
    std::map<std::string, glm::vec3> colors;
    std::vector<prop_part> parts;
    std::map<std::string, glm::vec3> points;

    // Colours and points the code asks for are checked when the prop loads.
    glm::vec3 color(const std::string& name) const { return colors.at(name); }
    glm::vec3 point(const std::string& name) const { return points.at(name); }
};

// nullopt when a colour is not three numbers in 0..1, a part's shape,
// sizes or colour name is wrong, a glow is not a glow floor, or any of
// `needed_colors` or `needed_points` is missing.
std::optional<prop_model> parse_prop_from_text(const std::string& text,
                                               const std::vector<std::string>& needed_colors,
                                               const std::vector<std::string>& needed_points);

struct prop_set {
    prop_model cigarette;
    prop_model beer_can;
    prop_model golf_cart;
    prop_model tree;
    prop_model fence;
    prop_model floodlight;
    prop_model hole_sign;
};

struct prop_set_load_result {
    std::optional<prop_set> props;
    std::string error;  // the file that failed
};

prop_set_load_result load_props(const std::string& asset_root);
