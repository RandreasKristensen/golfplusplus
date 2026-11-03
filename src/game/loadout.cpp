#include "game/loadout.h"

#include "game/json_util.h"

#include <limits>

namespace {
bool is_kind(const item_catalog& items, const std::string& id, const item_kind kind) {
    const item_definition* item = items.find(id);
    return item != nullptr && item->kind == kind;
}

bool mounts_fit(const std::map<std::string, std::string>& mounts, const item_catalog& items) {
    for (const auto& [mount, trinket] : mounts) {
        if (mount.empty() || !is_kind(items, trinket, item_kind::trinket)) {
            return false;
        }
    }
    return true;
}

// Every item equipped, cart first.
std::vector<std::string> equipped_ids(const loadout& equipped) {
    std::vector<std::string> ids{equipped.cart};
    for (const auto& [mount, trinket] : equipped.cart_mounts) {
        ids.push_back(trinket);
    }
    ids.push_back(equipped.smokable);
    ids.push_back(equipped.drinkable);
    ids.push_back(equipped.bag);
    return ids;
}
}

std::optional<loadout> parse_loadout_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root) {
        return std::nullopt;
    }
    const std::optional<std::string> cart = json_string(*root, "cart");
    const std::optional<std::string> smokable = json_string(*root, "smokable");
    const std::optional<std::string> drinkable = json_string(*root, "drinkable");
    const std::optional<std::string> bag = json_string(*root, "bag");
    const json* mounts = json_object(*root, "cart_mounts");
    if (!cart || !smokable || !drinkable || !bag || mounts == nullptr || !mounts->is_object()) {
        return std::nullopt;
    }
    loadout parsed{*cart, {}, *smokable, *drinkable, *bag};
    for (const auto& [mount, trinket] : mounts->items()) {
        if (!trinket.is_string()) {
            return std::nullopt;
        }
        parsed.cart_mounts[mount] = trinket.get<std::string>();
    }
    return parsed;
}

bool loadout_fits(const loadout& equipped, const item_catalog& items) {
    return is_kind(items, equipped.cart, item_kind::cart) && is_kind(items, equipped.smokable, item_kind::smokable) &&
           is_kind(items, equipped.drinkable, item_kind::drinkable) && is_kind(items, equipped.bag, item_kind::bag) &&
           mounts_fit(equipped.cart_mounts, items);
}

loadout fitted_loadout(const loadout& saved, const loadout& defaults, const item_catalog& items) {
    loadout fitted = defaults;
    if (is_kind(items, saved.cart, item_kind::cart)) {
        fitted.cart = saved.cart;
        fitted.cart_mounts = mounts_fit(saved.cart_mounts, items) ? saved.cart_mounts : defaults.cart_mounts;
    }
    if (is_kind(items, saved.smokable, item_kind::smokable)) {
        fitted.smokable = saved.smokable;
    }
    if (is_kind(items, saved.drinkable, item_kind::drinkable)) {
        fitted.drinkable = saved.drinkable;
    }
    if (is_kind(items, saved.bag, item_kind::bag)) {
        fitted.bag = saved.bag;
    }
    return fitted;
}

std::vector<active_effect> equipped_effects(const loadout& equipped, const item_catalog& items) {
    std::vector<active_effect> effects;
    for (const std::string& id : equipped_ids(equipped)) {
        const item_definition* item = items.find(id);
        if (item == nullptr) {
            continue;
        }
        for (const item_effect& effect : item->effects) {
            if (effect.duration == effect_duration::equipped) {
                effects.push_back(active_effect{id, effect, std::numeric_limits<std::int64_t>::min(), effect_never_ends});
            }
        }
    }
    return effects;
}
