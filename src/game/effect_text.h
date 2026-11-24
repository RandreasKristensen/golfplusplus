#pragma once

// The words of the effect shelf (game/effect_shelf.h): a square's seconds
// left and the lines saying what an effect changes, from the string table
// (en.json), never from code. Pure.

#include "game/effects.h"
#include "game/text_assets.h"

#include <string>

// `45` up to 99 seconds, then whole minutes rounded up with a suffix: `2M`.
std::string effect_seconds_text(const text_assets& text, int seconds);

// What `target` is called ("WALKING SPEED", "ALL XP", "PUTTING XP"); a marked
// missing string for a target it has no name for.
std::string effect_target_text(const text_assets& text, const std::string& target);

// One effect as a line: a signed whole percent for increased and more
// ("-30% WALKING SPEED"), a signed number for flat.
std::string effect_change_text(const text_assets& text, const item_effect& effect);
