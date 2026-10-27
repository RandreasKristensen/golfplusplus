#pragma once

// A club: an item of the club kind (assets/play/items/club/,
// game/item_catalog.h), as the game plays it.

#include "physics/impact.h"

#include <string>

struct club_definition {
    std::string id;
    std::string name;          // a string id (assets/interface/text/en.json)
    std::string label;         // short HUD label, e.g. "7I"
    std::string club_class;    // driver, woods, hybrids, long_irons, short_irons, wedges or putter: its skill
    std::string hit_sound;     // sound ids in assets/sound/sounds.json: struck in the middle,
    std::string mishit_sound;  // and off it
    std::string model;         // its model in assets/world/props/
    int bag_order = 0;         // place in the bag, longest club first
    club_spec spec;
};
