#pragma once

// The things a player uses, one file each in assets/play/items/<kind>/,
// keyed by file name: what it is (its kind is its folder), which model it
// looks like (game/prop_loader.h), its name in the string table, how it is
// used and what it does while it lasts (game/effects.h). Rules only, so the
// server module compiles them in; looks are the model's. A file may name a
// `base` item of its kind and give only what differs: its `use` merges into
// the base's member by member, anything else replaces the base's. A new
// cigarette, beer, cart or trinket is files, never code.

#include "game/club_definition.h"
#include "game/effects.h"
#include "game/reward_rules.h"
#include "physics/cart_dynamics.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* items_path = "play/items";

enum class item_kind {
    smokable,
    drinkable,
    cart,
    trinket,
    club,
    bag  // what the clubs are carried in (game/golf_bag.h): a model only, for now
};

// The folder an item of `kind` is in, and the kind of a folder.
const char* item_kind_name(item_kind kind);
std::optional<item_kind> item_kind_from_name(const std::string& name);

// Using it: it plays for `seconds_per_use` and cannot be used again
// meanwhile; each use earns `xp_per_use` (none when unset) and plays `sound`
// (assets/sound/sounds.json).
struct item_use_rules {
    float seconds_per_use = 0.0f;
    std::optional<xp_reward> xp_per_use;
    std::string sound;
};

// Using it again while its effects last: `refresh` restarts them, `stack`
// adds another of each.
enum class item_stacking {
    refresh,
    stack
};

struct item_definition {
    std::string id;
    item_kind kind = item_kind::smokable;
    std::string model;  // a model in assets/world/props/
    std::string name;   // a string id (assets/interface/text/en.json)
    std::optional<item_use_rules> use;  // smokables and drinkables
    std::optional<club_definition> club;  // clubs: everything but its id, name and model, which are the item's
    std::optional<cart_spec> cart;        // carts: its build (physics/cart_dynamics.h)
    item_stacking stacking = item_stacking::refresh;
    std::vector<item_effect> effects;
};

struct item_catalog {
    std::map<std::string, item_definition> items;

    // nullptr when there is no such item.
    const item_definition* find(const std::string& id) const;
};

// One item, `id` of `kind`, from its resolved file; nullopt when a field is
// missing or wrong: a smokable or drinkable needs its use, a club or cart
// its spec, an effect a target, a bucket, a value and a duration (`instant`,
// `use`, `equipped` or {"seconds": n}), and `use` effects need a use.
std::optional<item_definition> parse_item(const std::string& id, item_kind kind, const std::string& text);

struct item_catalog_load_result {
    std::optional<item_catalog> catalog;
    std::string error;  // the file that failed
};

// Every item file's text by its path under items_path ("smokable/cigarette.json").
item_catalog_load_result parse_item_catalog(const std::map<std::string, std::string>& texts);

// Every club item, longest first (bag_order).
std::vector<club_definition> clubs_of(const item_catalog& items);

// The effects running at `now_ms` once `item` is used then: its timed effects
// start, each lasting its seconds or the use's, and with `refresh` the item's
// own from an earlier use are dropped first.
std::vector<active_effect> effects_after_use(std::vector<active_effect> running, const item_definition& item,
                                             std::int64_t now_ms);

// Whether a use of `item` from `used_at_ms` still plays at `now_ms`.
bool item_use_playing(const item_definition& item, std::int64_t used_at_ms, std::int64_t now_ms);
