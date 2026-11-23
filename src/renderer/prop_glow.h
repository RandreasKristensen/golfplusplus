#pragma once

// A static model's glow floors as slots, not values: the night raises some of
// them every dusk, so a cached vertex names the slot (a glow below zero,
// prop.vert) and each frame hands the shader its floors. GL-free.

#include "game/lighting_fields.h"
#include "game/lighting_loader.h"

#include <array>
#include <iterator>

#include <glm/vec4.hpp>

inline constexpr std::size_t glow_slot_count = std::size(glow_fields);
static_assert(glow_slot_count <= 8, "prop.vert holds eight glow floors");

// Every floor replaced by its slot, -(index + 1): what append_prop_model puts in a vertex.
inline glow_floors glow_floor_slots() {
    glow_floors slots;
    float slot = -1.0f;
    for (const share_field<glow_floors>& field : glow_fields) {
        slots.*field.member = slot;
        slot -= 1.0f;
    }
    return slots;
}

// `glow` laid out by slot for prop.vert's u_glow_floors.
inline std::array<glm::vec4, 2> glow_floor_table(const glow_floors& glow) {
    std::array<glm::vec4, 2> table{glm::vec4(0.0f), glm::vec4(0.0f)};
    std::size_t slot = 0;
    for (const share_field<glow_floors>& field : glow_fields) {
        table[slot / 4][static_cast<int>(slot % 4)] = glow.*field.member;
        ++slot;
    }
    return table;
}
