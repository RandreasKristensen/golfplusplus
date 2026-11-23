#include "renderer/falling_leaf.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/geometric.hpp>

namespace {
constexpr float tau = 6.2831853f;
constexpr float seconds_per_day = 86400.0f;
// A leaf starts no higher than the instance's crown top (render_tree.cpp's crown_height).
constexpr float highest_start = 1.3f;
// A leaf's tumble, and the height over the ground by which it has levelled out.
constexpr float tumble_radians = 0.8f;
constexpr float level_meters = 0.2f;
constexpr float most_spin = 1.5f;  // radians a second
constexpr float lying_lift = 0.005f;
// The share of its lying time over which a leaf shrinks away.
constexpr float shrinking_share = 0.3f;

std::uint32_t bits(const float value) {
    std::uint32_t out = 0;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

// 0..1 (24 bits) from a leaf's key and what is drawn for it.
float unit(const std::uint32_t key, const std::uint32_t index, const std::uint32_t cycle, const std::uint32_t draw) {
    const std::uint32_t mixed = key ^ falling_leaf_hash(index * 0x9e3779b9U + cycle * 0x85ebca6bU + draw * 0xc2b2ae35U);
    return static_cast<float>(falling_leaf_hash(mixed) >> 8) * (1.0f / 16777216.0f);
}

// A slot's cycle at a moment: which one it is, how far into it, and whether it drops a leaf.
struct slot_cycle {
    std::uint32_t key = 0;
    std::uint32_t round = 0;
    float age = 0.0f;
    bool used = false;
};

slot_cycle cycle_of(const falling_leaf_style& style, const render_tree_instance& tree, const int index, const float per_cycle,
                    const float seconds) {
    slot_cycle out;
    const float cycle_seconds = falling_leaf_cycle_seconds(style, tree);
    out.key =
        falling_leaf_hash(bits(tree.offset.x) + falling_leaf_hash(bits(tree.offset.z) + falling_leaf_hash(bits(tree.offset.y))));
    const std::uint32_t slot = static_cast<std::uint32_t>(index);
    const float shifted = seconds + unit(out.key, slot, 0U, 0U) * cycle_seconds;
    const float cycle = std::floor(shifted / cycle_seconds);
    out.age = shifted - cycle * cycle_seconds;
    out.round = static_cast<std::uint32_t>(static_cast<std::int32_t>(cycle));
    out.used = unit(out.key, slot, out.round, 1U) < std::clamp(per_cycle - static_cast<float>(index), 0.0f, 1.0f);
    return out;
}
}

std::uint32_t falling_leaf_hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float falling_leaf_cycle_seconds(const falling_leaf_style& style, const render_tree_instance& tree) {
    return highest_start * tree.scale.y / style.fall_speed + style.lie_seconds;
}

float falling_leaves_per_cycle(const falling_leaf_style& style, const render_tree_instance& tree, const float daily_share,
                            const float wind_speed) {
    const float leaves = style.leaves_per_square_meter * 3.14159265f * tree.scale.x * tree.scale.z;
    return leaves * std::max(daily_share, 0.0f) / seconds_per_day * falling_leaf_cycle_seconds(style, tree) *
           (1.0f + style.wind_factor * wind_speed);
}

bool falling_leaf_slot_used(const falling_leaf_style& style, const render_tree_instance& tree, const int index,
                            const float per_cycle, const float seconds) {
    return cycle_of(style, tree, index, per_cycle, seconds).used;
}

falling_leaf place_falling_leaf(const falling_leaf_style& style, const tree_box& crown, const render_tree_instance& tree,
                                const int index, const float per_cycle, const float seconds, const glm::vec2& wind) {
    falling_leaf leaf;
    const slot_cycle at = cycle_of(style, tree, index, per_cycle, seconds);
    if (!at.used) {
        return leaf;
    }
    const std::uint32_t key = at.key;
    const std::uint32_t slot = static_cast<std::uint32_t>(index);
    const std::uint32_t round = at.round;
    const float age = at.age;
    // From a random point of the crown's surface.
    const float rise = 2.0f * unit(key, slot, round, 2U) - 1.0f;
    const float around = tau * unit(key, slot, round, 3U);
    const float ring = std::sqrt(std::max(1.0f - rise * rise, 0.0f));
    const glm::vec3 local = crown.centre + glm::vec3(ring * std::cos(around), rise, ring * std::sin(around)) * crown.half;
    const glm::vec3 start = tree_world_point(tree, local);
    const float fall_seconds = std::max(start.y - tree.offset.y, 0.0f) / style.fall_speed;
    if (age > fall_seconds + style.lie_seconds) {
        return leaf;
    }
    leaf.lying = age > fall_seconds;
    const float flying = std::min(age, fall_seconds);
    const float sway_way = tau * unit(key, slot, round, 4U);
    const float flutter = tau * unit(key, slot, round, 5U);
    const float spin = (2.0f * unit(key, slot, round, 6U) - 1.0f) * most_spin;
    const float swing = std::sin(tau * flying / style.flutter_seconds + flutter);
    leaf.centre = start + glm::vec3(std::cos(sway_way), 0.0f, std::sin(sway_way)) * (style.flutter_meters * swing) +
                  glm::vec3(wind.x, 0.0f, wind.y) * flying;
    leaf.centre.y = leaf.lying ? tree.offset.y + lying_lift : start.y - style.fall_speed * flying;
    const float height = leaf.centre.y - tree.offset.y;
    const float tilt = leaf.lying ? 0.0f : tumble_radians * swing * std::clamp(height / level_meters, 0.0f, 1.0f);
    const float turn = spin * flying + flutter;
    const float size = 0.5f * style.leaf_size_meters *
        (leaf.lying ? std::clamp((fall_seconds + style.lie_seconds - age) / (shrinking_share * style.lie_seconds), 0.0f, 1.0f)
                    : 1.0f);
    const glm::vec3 level(std::cos(turn), 0.0f, std::sin(turn));
    leaf.along = glm::vec3(-std::sin(turn), 0.0f, std::cos(turn)) * size;
    leaf.across = (level * std::cos(tilt) + glm::vec3(0.0f, 1.0f, 0.0f) * std::sin(tilt)) * size;
    // No corner dips under the ground while it levels out.
    leaf.centre.y = std::max(leaf.centre.y, tree.offset.y + std::abs(leaf.across.y));
    leaf.shown = size > 0.0f;
    return leaf;
}
