#pragma once

// Where the XP drops stand (render_xp_drop): under the club label, right-aligned
// with it, each falling a modest way and fading out over its life. The newest
// (last) starts in the top slot; an older one never overlaps a newer one, it
// waits a line lower. Only the newest few are shown. GL-free.

#include "renderer/render_data.h"
#include "renderer/ui_rect.h"

#include <cstddef>
#include <vector>

struct xp_drop_slot {
    std::size_t drop = 0;  // index into the drops given
    ui_rect box;           // icon on the left, text right of it
    float alpha = 1.0f;
};

inline constexpr std::size_t xp_drops_shown = 4;

// Slots for the newest drops, newest first.
std::vector<xp_drop_slot> layout_xp_drops(const std::vector<render_xp_drop>& drops);
