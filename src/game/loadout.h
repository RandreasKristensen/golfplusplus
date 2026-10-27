#pragma once

// What a player has equipped, slot by slot, as item ids
// (game/item_catalog.h): their cart and what hangs at its mounts, what they
// smoke and what they drink, and their bag. Only the defaults exist
// (assets/play/items/default_loadout.json): the offline save and the
// server's player_loadout row hold the ids, so owning and choosing items
// fit in later. Others see my cart and what I smoke from my row.

#include "game/effects.h"
#include "game/item_catalog.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* default_loadout_path = "play/items/default_loadout.json";

struct loadout {
    std::string cart;
    std::map<std::string, std::string> cart_mounts;  // a mount of the cart's model -> a trinket
    std::string smokable;
    std::string drinkable;
    std::string bag;
};

// nullopt when a slot is missing; whether the ids name items is
// loadout_fits's to say.
std::optional<loadout> parse_loadout_from_text(const std::string& text);

// Each slot names an item of its kind in `items` (an empty mount list is fine).
bool loadout_fits(const loadout& equipped, const item_catalog& items);

// `saved` where it fits `items`, else the default, slot by slot.
loadout fitted_loadout(const loadout& saved, const loadout& defaults, const item_catalog& items);

// Every equipped item's `equipped` effects, running from ever to never.
std::vector<active_effect> equipped_effects(const loadout& equipped, const item_catalog& items);
