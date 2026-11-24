#pragma once

// Where the top-right HUD boxes stand in overlay clip space, so what is laid
// out beside them (the effect shelf) keeps clear of them. GL-free.

#include "renderer/ui_rect.h"

// The club's short name over its full name, under the VCR tag.
inline ui_rect club_label_box() {
    return rect_from_edges(0.60f, 0.64f, 0.95f, 0.88f);
}

// Top centre, clear of the stroke ticks, cart panel, club and XP drops. Empty offline.
inline ui_rect mode_label_box() {
    return rect_from_edges(-0.27f, 0.85f, 0.27f, 0.97f);
}
