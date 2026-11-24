#include "renderer/xp_drop_layout.h"

#include "renderer/hud_boxes.h"

#include <algorithm>

namespace {
// Overlay clip units: a drop is two text lines tall, starts a hair under the club label and falls this far.
constexpr float drop_half_width = 0.155f;
constexpr float drop_line = 0.088f;
constexpr float start_gap = 0.010f;
constexpr float fall = 0.16f;
// Full alpha for the first part of the life, then linearly to nothing.
constexpr float fade_from = 0.25f;
}

std::vector<xp_drop_slot> layout_xp_drops(const std::vector<render_xp_drop>& drops) {
    std::vector<xp_drop_slot> slots;
    const ui_rect club = club_label_box();
    const float right = rect_right(club);
    const float start_top = rect_bottom(club) - start_gap;
    float previous_top = start_top + drop_line;
    for (std::size_t n = 0; n < drops.size() && slots.size() < xp_drops_shown; ++n) {
        const std::size_t index = drops.size() - 1 - n;
        if (drops[index].xp <= 0) {
            continue;
        }
        const float progress = std::clamp(drops[index].progress, 0.0f, 1.0f);
        const float top = std::min(start_top - fall * progress, previous_top - drop_line);
        previous_top = top;
        xp_drop_slot slot;
        slot.drop = index;
        slot.box = rect_from_edges(right - 2.0f * drop_half_width, top - drop_line, right, top);
        slot.alpha = progress > fade_from ? 1.0f - (progress - fade_from) / (1.0f - fade_from) : 1.0f;
        slots.push_back(slot);
    }
    return slots;
}
