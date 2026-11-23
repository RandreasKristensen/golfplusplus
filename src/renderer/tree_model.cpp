#include "renderer/tree_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

#include <glm/geometric.hpp>

namespace {
constexpr float tau = 6.2831853f;
// Per order: the length of a drawn segment (crown radii) and the most segments a stem is drawn in.
constexpr float segment_length[3] = {0.35f, 0.5f, 1.0f};
constexpr int most_segments[3] = {4, 2, 1};
// Twigs begin this share of a card's length from its foot: the card overlaps its branch's end.
constexpr float card_foot = 0.2f;
// The furthest out in the crown's shape a card's far end reaches.
constexpr float card_reach = 1.15f;
// The two side cards of a clump lean this far off its branch, opposite ways, so
// the spray fans out and no view sees all three cards edge on.
constexpr float card_fan = 0.5f;
// A branch end narrows to its spray's leading twig (tree_leaf.glsl: 0.012 of a card's width).
constexpr float twig_share_of_card = 0.012f;

// Triangles are collected as a plain list; index_vertices shares their vertices.
void add_triangle(grown_tree& model, const grown_tree_vertex& a, const grown_tree_vertex& b, const grown_tree_vertex& c) {
    model.vertices.push_back(a);
    model.vertices.push_back(b);
    model.vertices.push_back(c);
}

// Turns the triangle list in `model.vertices` into unique vertices and the
// indices of the same triangles in the same order. Vertices are the same only
// when every attribute is bit for bit.
void index_vertices(grown_tree& model) {
    static_assert(sizeof(grown_tree_vertex) == 12 * sizeof(float), "grown_tree_vertex must have no padding");
    std::vector<grown_tree_vertex> unique;
    std::unordered_map<std::string, std::uint32_t> found;
    model.indices.reserve(model.vertices.size());
    for (const grown_tree_vertex& vertex : model.vertices) {
        const auto [at, added] = found.try_emplace(std::string(reinterpret_cast<const char*>(&vertex), sizeof vertex),
                                                   static_cast<std::uint32_t>(unique.size()));
        if (added) {
            unique.push_back(vertex);
        }
        model.indices.push_back(at->second);
    }
    model.vertices = std::move(unique);
}

glm::vec3 across_of(const glm::vec3& axis) {
    const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(axis, helper));
}

// A stem as a bent tube of its order's sides through `points` (true frame),
// its rings carried along without twisting; normals out from its axis. A
// stem standing on the ground starts in a level ring.
void add_tube(grown_tree& model, const tree_true_frame& frame, const std::vector<glm::vec3>& points,
              const std::vector<float>& radii, const int sides, const bool grounded, const float fineness) {
    const std::size_t count = points.size();
    std::vector<glm::vec3> axes(count);
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 before = points[i] - points[i == 0 ? 0 : i - 1];
        const glm::vec3 after = points[std::min(i + 1, count - 1)] - points[i];
        const glm::vec3 sum = (glm::length(before) > 1.0e-7f ? glm::normalize(before) : glm::vec3(0.0f)) +
                              (glm::length(after) > 1.0e-7f ? glm::normalize(after) : glm::vec3(0.0f));
        axes[i] = glm::length(sum) > 1.0e-6f ? glm::normalize(sum) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    if (grounded) {
        axes[0] = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    std::vector<std::vector<grown_tree_vertex>> rings(count);
    glm::vec3 across = across_of(axes[0]);
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 carried = across - axes[i] * glm::dot(across, axes[i]);
        across = glm::length(carried) > 1.0e-4f ? glm::normalize(carried) : across_of(axes[i]);
        const glm::vec3 other = glm::cross(axes[i], across);
        for (int k = 0; k <= sides; ++k) {
            const float angle = tau * static_cast<float>(k % sides) / static_cast<float>(sides);
            const glm::vec3 out = across * std::cos(angle) + other * std::sin(angle);
            grown_tree_vertex vertex;
            vertex.position = frame.unit(points[i] + out * radii[i]);
            vertex.normal = frame.unit_normal(out);
            vertex.depth = fineness;
            rings[i].push_back(vertex);
        }
    }
    for (std::size_t i = 0; i + 1 < count; ++i) {
        for (int k = 0; k < sides; ++k) {
            const std::size_t a = static_cast<std::size_t>(k);
            const std::size_t b = a + 1;
            add_triangle(model, rings[i][a], rings[i][b], rings[i + 1][b]);
            add_triangle(model, rings[i][a], rings[i + 1][b], rings[i + 1][a]);
        }
    }
}

// A stem's nodes resampled into its order's segments, evenly along its length;
// one ending in a clump narrows along it to `twig_radius`.
void add_stem(grown_tree& model, const tree_true_frame& frame, const tree_skeleton& skeleton, const tree_skeleton::stem& stem,
              const float twig_radius) {
    std::vector<float> along{0.0f};
    for (std::size_t k = 1; k < stem.nodes.size(); ++k) {
        along.push_back(along.back() + glm::length(skeleton.nodes[static_cast<std::size_t>(stem.nodes[k])].position -
                                                   skeleton.nodes[static_cast<std::size_t>(stem.nodes[k - 1])].position));
    }
    const float length = along.back();
    if (length <= 1.0e-6f) {
        return;
    }
    const int order = std::clamp(stem.order, 0, 2);
    const int segments = std::clamp(static_cast<int>(std::ceil(length / segment_length[order])), 1, most_segments[order]);
    std::vector<glm::vec3> points;
    std::vector<float> radii;
    std::size_t k = 1;
    for (int s = 0; s <= segments; ++s) {
        const float at = length * static_cast<float>(s) / static_cast<float>(segments);
        while (k + 1 < along.size() && along[k] < at) {
            ++k;
        }
        const float t = std::clamp((at - along[k - 1]) / std::max(along[k] - along[k - 1], 1.0e-6f), 0.0f, 1.0f);
        const tree_skeleton::node& from = skeleton.nodes[static_cast<std::size_t>(stem.nodes[k - 1])];
        const tree_skeleton::node& to = skeleton.nodes[static_cast<std::size_t>(stem.nodes[k])];
        points.push_back(from.position + (to.position - from.position) * t);
        // A branch leaves its parent as thick as its own first node.
        const float from_radius = k == 1 && stem.order > 0 ? skeleton.nodes[static_cast<std::size_t>(stem.nodes[1])].radius : from.radius;
        radii.push_back(from_radius + (to.radius - from_radius) * t);
    }
    if (twig_radius > 0.0f) {
        const float narrowing = std::max(radii.back() - twig_radius, 0.0f);
        for (std::size_t s = 0; s < radii.size(); ++s) {
            radii[s] -= narrowing * static_cast<float>(s) / static_cast<float>(radii.size() - 1);
        }
    }
    add_tube(model, frame, points, radii, tree_order_sides[order], skeleton.nodes[static_cast<std::size_t>(stem.nodes[0])].parent < 0,
             0.5f * static_cast<float>(order));
}

// Three cards crossed along a clump's way from its branch end, `size` wide
// (longer, hanging, for a weeping crown's whips); lit as part of the crown.
struct card_clump {
    glm::vec3 foot{0.0f};  // true frame
    glm::vec3 along{0.0f};
    float size = 0.0f;
    float length = 0.0f;
    glm::vec3 middle{0.0f};  // unit frame
    float hold = 0.0f;
};

void add_cards(grown_tree& model, const tree_look& look, const tree_true_frame& frame, const card_clump& clump, tree_dice& dice) {
    const tree_box box = crown_shape_box(look);
    const float reach = std::min(crown_reach(look, clump.middle), 1.0f);
    const glm::vec3 outward = glm::normalize((clump.middle - box.centre) / box.half + glm::vec3(0.0f, 0.15f, 0.0f));
    const glm::vec3 first = across_of(clump.along);
    const glm::vec3 second = glm::cross(clump.along, first);
    // Turned at random about the branch: no card lies level on every clump.
    const float turn = dice.between(0.0f, 1.0f / 3.0f);
    const glm::vec3 tip = clump.foot + clump.along * (card_foot * clump.length);
    for (int card = 0; card < 3; ++card) {
        const float angle = 0.5f * tau * (static_cast<float>(card) / 3.0f + turn);
        const glm::vec3 across = first * std::cos(angle) + second * std::sin(angle);
        const glm::vec3 face = glm::cross(across, clump.along);
        const float lean = card == 0 ? 0.0f : (card == 1 ? card_fan : -card_fan);
        const glm::vec3 along = clump.along * std::cos(lean) + face * std::sin(lean);
        const glm::vec3 foot = tip - along * (card_foot * clump.length);
        const glm::vec3 normal = glm::normalize(outward * 0.8f + frame.unit_normal(glm::cross(across, along)) * 0.2f);
        const float seed = dice.next();
        const auto corner = [&](const float u, const float v) {
            grown_tree_vertex out;
            out.position = frame.unit(foot + across * ((u - 0.5f) * clump.size) + along * (v * clump.length));
            out.normal = normal;
            out.uv = glm::vec2(u, v);
            out.leaf = 1.0f;
            out.depth = 1.0f - reach;
            out.hold = clump.hold;
            out.seed = seed;
            return out;
        };
        add_triangle(model, corner(0.0f, 0.0f), corner(1.0f, 0.0f), corner(1.0f, 1.0f));
        add_triangle(model, corner(0.0f, 0.0f), corner(1.0f, 1.0f), corner(0.0f, 1.0f));
    }
}
}

grown_tree grow_tree(const tree_look& look, const std::uint32_t seed) {
    grown_tree model;
    // The cards' own numbers, apart from the skeleton's.
    tree_dice dice(static_cast<std::uint64_t>(seed) ^ 0x5bd1e995ULL);
    const tree_true_frame frame{std::max(look.aspect, 0.1f)};
    tree_skeleton skeleton = grow_skeleton(look, seed);
    for (tree_skeleton::node& node : skeleton.nodes) {
        node.position = frame.lifted(node.position);
    }
    for (tree_skeleton::clump& clump : skeleton.clumps) {
        clump.base = frame.lifted(clump.base);
        clump.direction = glm::normalize(frame.lifted(clump.direction));
    }
    std::vector<char> parent(skeleton.nodes.size(), 0);
    for (const tree_skeleton::node& node : skeleton.nodes) {
        if (node.parent >= 0) {
            parent[static_cast<std::size_t>(node.parent)] = 1;
        }
    }
    for (const tree_skeleton::stem& stem : skeleton.stems) {
        const bool ends = !parent[static_cast<std::size_t>(stem.nodes.back())];
        add_stem(model, frame, skeleton, stem, ends ? twig_share_of_card * look.clump_size : 0.0f);
    }

    // The clumps: hanging as the look says, kept off the ground, holding their
    // withered leaves the more the lower and further in they are.
    const tree_box box = crown_shape_box(look);
    const float crown_base = look.shape == crown_shape::dome ? 0.0f : box.centre.y - box.half.y;
    const float crown_span = std::max(box.centre.y + box.half.y - crown_base, 1.0e-3f);
    std::vector<card_clump> clumps;
    float hold_sum = 0.0f;
    for (const tree_skeleton::clump& at : skeleton.clumps) {
        card_clump clump;
        clump.along = glm::normalize(at.direction - glm::vec3(0.0f, look.droop, 0.0f));
        clump.size = look.clump_size * dice.between(0.8f, 1.2f);
        clump.length = clump.size * (1.0f + look.droop);
        clump.foot = at.base - clump.along * (card_foot * clump.length);
        // Its far end, its top corner at most, stays near the crown's surface.
        const glm::vec3 corner(0.0f, 0.5f * clump.size, 0.0f);
        for (int pull = 0; pull < 8 && crown_reach(look, frame.unit(clump.foot + clump.along * clump.length + corner)) > card_reach;
             ++pull) {
            clump.foot -= clump.along * (0.1f * clump.length);
        }
        const float lowest = std::min(clump.foot.y, (clump.foot + clump.along * clump.length).y) - 0.5f * clump.size;
        clump.foot.y += std::max(0.0f, 0.01f * frame.aspect - lowest);
        clump.middle = frame.unit(clump.foot + clump.along * (0.5f * clump.length));
        const float inner = 1.0f - std::min(crown_reach(look, clump.middle), 1.0f);
        const float low = 1.0f - std::clamp((clump.middle.y - crown_base) / crown_span, 0.0f, 1.0f);
        const float lowly = 0.5f * inner + 0.5f * low;
        clump.hold = lowly * lowly * lowly;
        hold_sum += clump.hold;
        clumps.push_back(clump);
    }
    const float hold_scale = 0.5f * static_cast<float>(clumps.size()) / std::max(hold_sum, 1.0e-4f);
    for (card_clump& clump : clumps) {
        clump.hold = std::min(clump.hold * hold_scale, 1.0f);
        add_cards(model, look, frame, clump, dice);
    }
    index_vertices(model);
    return model;
}

tree_box tree_box_of(const grown_tree& model) {
    if (model.vertices.empty()) {
        return {};
    }
    glm::vec3 low = model.vertices.front().position;
    glm::vec3 high = low;
    for (const grown_tree_vertex& vertex : model.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    return {(low + high) * 0.5f, (high - low) * 0.5f};
}
