#include "renderer/tree_model.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
constexpr float tau = 6.2831853f;

// Deterministic 0..1 numbers (splitmix64), the same on every platform.
class tree_dice {
public:
    explicit tree_dice(const std::uint64_t seed) : state_(seed * 0x9e3779b97f4a7c15ULL + 0x632be59bd9b4e019ULL) {}

    float next() {
        std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        return static_cast<float>(z >> 40) / static_cast<float>(1ULL << 24);
    }

    float between(const float low, const float high) { return low + (high - low) * next(); }

private:
    std::uint64_t state_;
};

// The crown's middle and its half sizes in the unit frame.
struct crown_box {
    glm::vec3 middle{0.0f};
    glm::vec3 half{1.0f};
};

crown_box crown_of(const tree_look& look) {
    const float base = look.shape == crown_shape::dome ? 0.0f : std::clamp(look.trunk_share, 0.0f, 0.9f);
    const float span = 1.0f - base;
    switch (look.shape) {
    case crown_shape::dome:
        return {glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
    case crown_shape::umbrella:
        return {glm::vec3(0.0f, base + 0.6f * span, 0.0f), glm::vec3(1.0f, 0.4f * span, 1.0f)};
    case crown_shape::cone:
    case crown_shape::round:
    case crown_shape::oval:
    case crown_shape::weeping:
        break;
    }
    return {glm::vec3(0.0f, base + 0.5f * span, 0.0f), glm::vec3(1.0f, 0.5f * span, 1.0f)};
}

struct segment {
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    float from_radius = 0.0f;
    float to_radius = 0.0f;
};

void add_triangle(grown_tree& model, const grown_tree_vertex& a, const grown_tree_vertex& b, const grown_tree_vertex& c) {
    model.vertices.push_back(a);
    model.vertices.push_back(b);
    model.vertices.push_back(c);
}

// A tapered prism round `piece`, `sides` faces, its normals out from its axis.
void add_prism(grown_tree& model, const segment& piece, const int sides) {
    const glm::vec3 axis = glm::normalize(piece.to - piece.from + glm::vec3(0.0f, 1.0e-6f, 0.0f));
    const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 across = glm::normalize(glm::cross(axis, helper));
    const glm::vec3 other = glm::cross(axis, across);
    const auto ring = [&](const int k) {
        const float angle = tau * static_cast<float>(k) / static_cast<float>(sides);
        return across * std::cos(angle) + other * std::sin(angle);
    };
    for (int k = 0; k < sides; ++k) {
        const glm::vec3 a = ring(k);
        const glm::vec3 b = ring(k + 1);
        grown_tree_vertex v0;
        v0.position = piece.from + a * piece.from_radius;
        v0.normal = a;
        grown_tree_vertex v1;
        v1.position = piece.from + b * piece.from_radius;
        v1.normal = b;
        grown_tree_vertex v2;
        v2.position = piece.to + b * piece.to_radius;
        v2.normal = b;
        grown_tree_vertex v3;
        v3.position = piece.to + a * piece.to_radius;
        v3.normal = a;
        add_triangle(model, v0, v1, v2);
        add_triangle(model, v0, v2, v3);
    }
}

// The nearest point to `point` on `piece`, and how far along it is.
glm::vec3 nearest_on(const segment& piece, const glm::vec3& point, float& along) {
    const glm::vec3 d = piece.to - piece.from;
    const float length_squared = std::max(glm::dot(d, d), 1.0e-8f);
    along = std::clamp(glm::dot(point - piece.from, d) / length_squared, 0.0f, 1.0f);
    return piece.from + d * along;
}

// A point inside the crown near its surface (`reach_low`..`reach_high` of the
// way out from its middle), found by trying random points in its box.
glm::vec3 point_in_crown(const tree_look& look, const crown_box& box, tree_dice& dice, const float reach_low,
                         const float reach_high) {
    for (int attempt = 0; attempt < 64; ++attempt) {
        const glm::vec3 p = box.middle + glm::vec3(dice.between(-1.0f, 1.0f), dice.between(-1.0f, 1.0f),
                                                  dice.between(-1.0f, 1.0f)) * box.half;
        const float reach = crown_reach(look, p);
        if (reach >= reach_low && reach <= reach_high && (look.shape != crown_shape::dome || p.y >= 0.05f)) {
            return p;
        }
    }
    return box.middle;
}

// Three crossed cards round `centre`, `size` across (taller for a weeping
// crown's hanging clumps), facing every way; lit as part of the crown.
void add_clump(grown_tree& model, const tree_look& look, const crown_box& box, const glm::vec3& centre, const float size,
               tree_dice& dice) {
    const float reach = std::min(crown_reach(look, centre), 1.0f);
    const glm::vec3 outward = glm::normalize((centre - box.middle) / box.half + glm::vec3(0.0f, 0.15f, 0.0f));
    // The unit frame is stretched aspect times as tall as wide by the
    // instance: the cards are squashed back so the leaves come out round.
    const float tall = 1.0f / std::max(look.aspect, 0.1f);
    const float hang = look.droop;
    for (int card = 0; card < 3; ++card) {
        const float yaw = tau * (static_cast<float>(card) / 3.0f + dice.between(0.0f, 0.33f));
        const float tilt = dice.between(-0.6f, 0.6f);
        const glm::vec3 right(std::cos(yaw), 0.0f, std::sin(yaw));
        const glm::vec3 up = glm::normalize(glm::vec3(-std::sin(yaw) * tilt, 1.0f, std::cos(yaw) * tilt));
        const glm::vec3 half_right = right * (0.5f * size);
        const glm::vec3 half_up = up * (0.5f * size * tall * (1.0f + hang));
        glm::vec3 middle = centre - glm::vec3(0.0f, 0.5f * size * tall * hang, 0.0f);
        // No card reaches under the ground.
        middle.y = std::max(middle.y, std::abs(half_up.y) + 0.01f);
        const glm::vec3 face = glm::normalize(glm::cross(right, up));
        const glm::vec3 normal = glm::normalize(outward * 0.8f + face * 0.2f);
        const auto corner = [&](const float u, const float v) {
            grown_tree_vertex out;
            out.position = middle + half_right * (2.0f * u - 1.0f) + half_up * (2.0f * v - 1.0f);
            out.normal = normal;
            out.uv = glm::vec2(u, v);
            out.leaf = 1.0f;
            out.depth = 1.0f - reach;
            return out;
        };
        add_triangle(model, corner(0.0f, 0.0f), corner(1.0f, 0.0f), corner(1.0f, 1.0f));
        add_triangle(model, corner(0.0f, 0.0f), corner(1.0f, 1.0f), corner(0.0f, 1.0f));
    }
}
}

float crown_reach(const tree_look& look, const glm::vec3& point) {
    const crown_box box = crown_of(look);
    const glm::vec3 d = (point - box.middle) / box.half;
    if (look.shape == crown_shape::cone) {
        // Widest at the crown's foot, a point at the tree's top.
        const float base = box.middle.y - box.half.y;
        const float up = (point.y - base) / (2.0f * box.half.y);
        const float allowed = std::max(1.0f - up, 0.0f);
        const float out = std::sqrt(point.x * point.x + point.z * point.z);
        if (up < 0.0f || up > 1.0f) {
            return 1.0f + std::abs(up < 0.0f ? up : up - 1.0f) * 4.0f;
        }
        return allowed <= 1.0e-4f ? 2.0f : out / allowed;
    }
    return glm::length(d);
}

grown_tree grow_tree(const tree_look& look, const std::uint32_t seed, const tree_detail detail) {
    grown_tree model;
    tree_dice dice(seed);
    const crown_box box = crown_of(look);
    const bool full = detail == tree_detail::full;
    const bool bush = look.shape == crown_shape::dome;
    const int sides = full ? 6 : 4;
    const float crown_base = box.middle.y - box.half.y;

    // The trunk: into the crown's middle, or a spruce's to its top.
    std::vector<segment> limbs;
    const float trunk_radius = look.trunk_radius;
    if (!bush) {
        const float top = look.shape == crown_shape::cone ? 0.97f : box.middle.y + 0.2f * box.half.y;
        add_prism(model, segment{glm::vec3(0.0f), glm::vec3(0.0f, top, 0.0f), trunk_radius, trunk_radius * 0.25f}, sides);
    }
    // The limbs: from the trunk (a bush's from the ground) out into the crown.
    const int limb_count = std::max(1, full ? look.limbs : std::max(2, look.limbs / 2));
    for (int i = 0; i < limb_count; ++i) {
        const float azimuth = tau * (static_cast<float>(i) + dice.between(-0.3f, 0.3f)) / static_cast<float>(limb_count);
        const glm::vec3 out(std::cos(azimuth), 0.0f, std::sin(azimuth));
        float start_y = 0.0f;
        float end_y = 0.0f;
        if (bush) {
            start_y = 0.0f;
            end_y = dice.between(0.3f, 0.8f);
        } else if (look.shape == crown_shape::cone) {
            start_y = crown_base + (0.95f - crown_base) * (static_cast<float>(i) + 0.5f) / static_cast<float>(limb_count);
            end_y = start_y - 0.04f;
        } else {
            start_y = crown_base + dice.between(0.0f, 0.35f) * 2.0f * box.half.y;
            end_y = box.middle.y + dice.between(-0.2f, 0.5f) * box.half.y;
        }
        glm::vec3 end = glm::vec3(0.0f, end_y, 0.0f) + out * 0.2f;
        // Out until it nears the crown's surface.
        for (int step = 0; step < 20 && crown_reach(look, end + out * 0.05f) < 0.8f; ++step) {
            end += out * 0.05f;
        }
        const float thick = (bush ? 0.6f : 0.55f) * trunk_radius;
        limbs.push_back(segment{glm::vec3(0.0f, start_y, 0.0f) + out * (bush ? dice.between(0.0f, 0.3f) : 0.0f), end,
                                thick, thick * 0.3f});
    }
    for (const segment& limb : limbs) {
        add_prism(model, limb, sides);
    }

    // The clumps and the twigs to them.
    const int clump_count = std::max(1, full ? look.clumps : look.far_clumps);
    const float size = look.clump_size * (full ? 1.0f : std::sqrt(static_cast<float>(look.clumps) /
                                                                  static_cast<float>(std::max(1, look.far_clumps))));
    std::vector<glm::vec3> centres;
    for (int i = 0; i < clump_count; ++i) {
        const bool inner = dice.next() < 0.12f;
        centres.push_back(point_in_crown(look, box, dice, inner ? 0.3f : 0.55f, inner ? 0.55f : 0.95f));
    }
    if (full) {
        for (const glm::vec3& centre : centres) {
            float best_distance = 1.0e9f;
            glm::vec3 best(0.0f);
            float best_radius = 0.0f;
            for (const segment& limb : limbs) {
                float along = 0.0f;
                const glm::vec3 on = nearest_on(limb, centre, along);
                const float distance = glm::length(on - centre);
                if (distance < best_distance) {
                    best_distance = distance;
                    best = on;
                    best_radius = limb.from_radius + (limb.to_radius - limb.from_radius) * along;
                }
            }
            if (best_distance > 0.02f) {
                add_prism(model, segment{best, centre, best_radius * 0.5f, best_radius * 0.15f}, 3);
            }
        }
    }
    model.bark_count = model.vertices.size();
    for (const glm::vec3& centre : centres) {
        add_clump(model, look, box, centre, size * dice.between(0.8f, 1.2f), dice);
    }
    return model;
}
