#pragma once

// The effects that things which happen to a player start, one file each in
// assets/play/effects/, keyed by file name: its name and description in the
// string table, its icon, whether it hurts, what it changes while it lasts
// (the same effect entries items use, game/effects.h, but always for a number
// of seconds) and optionally what it does to the screen. Rules and looks as
// data, so the server module compiles them in. A new effect is a file, never
// code. Pure.

#include "game/effects.h"
#include "game/json_util.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* effects_path = "play/effects";

// A coloured edge on the screen that fades toward the middle over `seconds`.
struct screen_vignette {
    glm::vec3 colour{0.0f};
    float seconds = 0.0f;
};

struct event_effect_definition {
    std::string id;
    std::string name;         // a string id (assets/interface/text/en.json)
    std::string description;  // a string id
    std::string icon;         // names the square's art
    bool harmful = false;     // a debuff: tells it from a buff
    std::vector<item_effect> effects;  // all for a number of seconds
    std::optional<screen_vignette> vignette;
};

struct effect_catalog {
    std::map<std::string, event_effect_definition> effects;

    // nullptr when there is no such effect.
    const event_effect_definition* find(const std::string& id) const;
};

// One effect entry of an item or event effect file: a target, a bucket, a
// value and a duration (`instant`, `use`, `equipped` or {"seconds": n}).
std::optional<item_effect> parse_effect_entry(const json& object);

// One event effect, `id`, from its file; nullopt when a field is missing or
// wrong: a name, description, icon, harmful, effects that are not empty and
// each last a number of seconds, and a vignette with a colour (0 to 1) and
// seconds above zero when there is a `screen`.
std::optional<event_effect_definition> parse_event_effect(const std::string& id, const std::string& text);

struct effect_catalog_load_result {
    std::optional<effect_catalog> catalog;
    std::string error;  // the file that failed
};

// Every effect file's text by its path under effects_path ("dazed.json").
effect_catalog_load_result parse_effect_catalog(const std::map<std::string, std::string>& texts);

// The effects running once `effect` starts at `now_ms` (which may be ahead of
// the clock, a ball still in flight): each of its entries over [now_ms, now_ms
// + its seconds), the event's earlier ones ended at `now_ms` (dropped if they
// had not begun by then), so it refreshes and never stacks.
std::vector<active_effect> effects_after_event(std::vector<active_effect> running, const event_effect_definition& effect,
                                               std::int64_t now_ms);

// When the running event `event_id` started, nullopt when none of its effects
// run at `now_ms`.
std::optional<std::int64_t> event_started_at(const std::vector<active_effect>& effects, const std::string& event_id,
                                             std::int64_t now_ms);
