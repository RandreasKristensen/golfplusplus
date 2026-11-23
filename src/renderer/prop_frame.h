#pragma once

// Every moving model of one frame (the carts, bags, handled gear, what
// others hold, the bus doors, my club and my figure's gear), built once and
// laid out in groups so each view draws the groups it shows from the one
// upload (renderer/prop_renderer.h): the picture's main and zoomed views,
// the mirrored views (my figure and its gear instead of my club and hand) and
// the sun's shadow map (what casts: carts, bags and the gear figures carry).
// Each group is a run of every mesh's instance list (prop_batch.h), so a view
// draws a group as one instanced draw per mesh. GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_batch.h"
#include "renderer/render_data.h"

#include <array>
#include <cstddef>

enum class prop_view { scene, mirror, sun_shadow };

// Instances from the start of one mesh's list.
struct prop_range {
    std::size_t first = 0;
    std::size_t count = 0;
};

// At most two runs of one mesh's list (opaque or blended) of the frame's batch.
struct prop_runs {
    std::array<prop_range, 2> run{};
    int count = 0;
};
// The runs of each mesh's list.
using prop_ranges = std::array<prop_runs, prop_mesh_count>;

class prop_frame {
public:
    // `door_progress`: how far the bus's doors have swung (0 shut, 1 open).
    void build(const render_data& data, const prop_catalog& props, const scene_colors& colors, float door_progress);

    const prop_batch& batch() const { return batch_; }
    // The runs `view` draws of the opaque or blended lists.
    prop_ranges ranges(prop_view view, bool blended) const;

private:
    enum group : std::size_t {
        carts,             // every view
        others_gear,       // every view
        doors,             // scene, mirror
        others_held,       // scene, mirror
        mine_in_view,      // scene: my club and hand
        mine_gear,         // mirror, shadow: my figure's gear
        mine_in_mirror,    // mirror: my figure's club and what it holds
        group_count
    };
    // How long each list was: [blended][mesh].
    using mark = std::array<std::array<std::size_t, prop_mesh_count>, 2>;
    void close(group finished);
    prop_range span(group first, group last, bool blended, std::size_t mesh) const;

    prop_batch batch_;
    std::array<mark, group_count> ends_{};  // where each group ends
};
