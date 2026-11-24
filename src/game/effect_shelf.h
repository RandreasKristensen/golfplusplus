#pragma once

// What the HUD's effect shelf shows: one square per running source (an item
// with three effects is one square), newest first, each with what it is
// called and drawn as, what it changes and how long it has left. Items give
// their name and kind; event effects (game/effect_catalog.h) their own name,
// description, icon and whether they hurt. Pure.

#include "game/effect_catalog.h"
#include "game/effects.h"
#include "game/item_catalog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct effect_square {
    effect_source source;
    std::string icon;         // an item's kind or an event effect's icon: names the square's art
    bool harmful = false;     // a debuff; items never are
    std::string name;         // a string id
    std::string description;  // a string id; empty for an item, which has none
    std::vector<item_effect> effects;  // what the source changes, as started
    // Whole seconds left, rounded up; none while it lasts as long as it is equipped.
    std::optional<int> seconds_left;
};

// The squares for the effects running at `now_ms`, the one started last first.
// A source missing from its catalog has no square.
std::vector<effect_square> effect_shelf(const std::vector<active_effect>& effects, const item_catalog& items,
                                        const effect_catalog& events, std::int64_t now_ms);
