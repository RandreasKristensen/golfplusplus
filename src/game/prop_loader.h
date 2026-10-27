#pragma once

// The models of things drawn in the world, one file each in
// assets/world/props/, keyed by file name: its kind (game/model_kinds.h),
// its colours by name, and for a thing of fixed shape the parts it is built
// from in its own frame (x left, y up, z forward), named points the code
// animates from (where a cigarette's smoke rises) and mounts other models
// hang at (a trinket on a cart). A file may name a `base` model of its kind
// and give only what differs: its colours, points and mounts replace the
// base's of the same name, its parts all of the base's. What is fitted to the
// course (a tree's size, a fence's line, a floodlight's pole) or moves (a
// cart's wheels) is the code's; its file holds its colours. Looks only: what
// an item does is its item file (game/item_catalog.h). GL-free.

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

// Where another model hangs on this one, in this one's frame: the hung
// model's `hang` point goes `at`, its z along `forward` and its y along `up`.
struct prop_mount {
    glm::vec3 at{0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

struct prop_model {
    std::string kind;
    std::map<std::string, glm::vec3> colors;
    std::vector<prop_part> parts;
    std::map<std::string, glm::vec3> points;
    std::map<std::string, prop_mount> mounts;

    // What the code asks for is checked by kind when the model loads.
    glm::vec3 color(const std::string& name) const { return colors.at(name); }
    glm::vec3 point(const std::string& name) const { return points.at(name); }
    const prop_mount& mount(const std::string& name) const { return mounts.at(name); }
};

// Every model by its file name (no extension).
using prop_catalog = std::map<std::string, prop_model>;

// One model with no base: nullopt when a colour is not three numbers in
// 0..1, a part's shape, sizes or colour name is wrong, a glow is not a glow
// floor, a mount's sides are not perpendicular, or its kind is unknown or
// lacks what that kind needs (model_kinds.h).
std::optional<prop_model> parse_prop_from_text(const std::string& text);

struct prop_catalog_load_result {
    std::optional<prop_catalog> props;
    std::string error;  // the file that failed
};

// Every file's text by its name: each `base` resolved (through as many
// levels as named, a cycle or a base of another kind refused), then parsed.
prop_catalog_load_result parse_prop_catalog(const std::map<std::string, std::string>& texts);
// Every file in props_path, and the catalog's own shipped models it must
// have (model_kinds.h's required_models).
prop_catalog_load_result load_props(const std::string& asset_root);
