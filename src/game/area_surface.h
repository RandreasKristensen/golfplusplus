#pragma once

// What the ground at a point is, and the one rule for it: the winning zone's
// material, else the holes' fairway or rough, else rough. sample_area (the
// ball, walking, driving) and every printed map (the hole signs' faces, the
// course map) go through surface_at, so a map shows what the game plays.
// Pure.

#include "game/play_area.h"

#include <cstddef>
#include <optional>

#include <glm/vec3.hpp>

struct area_surface {
    terrain_material material = terrain_material::rough;
    std::optional<zone_hit> zone;                // the winning zone, if any
    std::optional<terrain_sample> on_hole;       // the holes' winning sample, off every hole: none
};

area_surface surface_at(const play_area& area, const glm::vec3& position);

// Whether the ground mesh covers `position` (XZ): where a map has ground to show.
bool ground_at(const play_area& area, const glm::vec3& position);

// Whether `surface` (at `position`) is part of hole `hole`'s map: its own
// zones (a zone no hole owns is every hole's), its ribbon whatever lies on
// it, and always a bunker or water, which every hole's map shows alike.
bool hole_owns_surface(const play_area& area, const area_surface& surface, std::size_t hole, const glm::vec3& position);
