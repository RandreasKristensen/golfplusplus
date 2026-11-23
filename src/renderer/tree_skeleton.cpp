#include "renderer/tree_skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
constexpr float tau = 6.2831853f;
// Attraction points per leaf clump: enough for the branches to fill the crown.
constexpr int points_per_clump = 6;
// A branch grows half a twig spacing a step, pulled by points up to four twig spacings away.
constexpr float step_share = 0.5f;
constexpr float influence_share = 4.0f;
constexpr int most_steps = 160;
// Branch ends and attraction points stay this far out in the crown, so the clumps beyond them stay in it.
constexpr float tip_reach = 0.9f;
// Branch ends carry this share of the clumps; the rest grow from the sides of the finest branches.
constexpr float clumps_at_ends = 0.6f;
// How far a step may turn at random, so no branch grows dead straight.
constexpr float bend_jitter = 0.25f;

// Space colonization in the true frame.
tree_skeleton grow_true(const tree_look& look, tree_dice& dice, const tree_true_frame& frame) {
    const tree_box box = crown_shape_box(look);
    const bool bush = look.shape == crown_shape::dome;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float crown_base = bush ? 0.0f : box.centre.y - box.half.y;
    const float crown_top = std::min(box.centre.y + box.half.y, 0.97f);
    const float step = look.twig_spacing * step_share;
    const float influence = look.twig_spacing * influence_share;
    const float kill = look.twig_spacing;
    const auto inside = [&](const glm::vec3& p) {
        const glm::vec3 u = frame.unit(p);
        return crown_reach(look, u) <= tip_reach && u.y >= 0.02f;
    };

    // Attraction points, denser toward the crown's surface; a cone's as many
    // at every height, as a conifer is in needles to its top.
    std::vector<glm::vec3> points;
    const std::size_t wanted = static_cast<std::size_t>(std::max(1, look.clumps) * points_per_clump);
    const bool cone = look.shape == crown_shape::cone;
    for (std::size_t attempt = 0; points.size() < wanted && attempt < wanted * 400; ++attempt) {
        glm::vec3 p = box.centre + glm::vec3(dice.between(-1.0f, 1.0f), dice.between(-1.0f, 1.0f),
                                            dice.between(-1.0f, 1.0f)) * box.half;
        if (cone) {
            const float rise = (p.y - crown_base) / std::max(crown_top - crown_base, 1.0e-3f);
            p.x *= std::max(1.0f - rise, 0.0f);
            p.z *= std::max(1.0f - rise, 0.0f);
        }
        const float reach = crown_reach(look, p) / tip_reach;
        if (reach > 1.0f || p.y < 0.05f || dice.next() > 0.2f + 0.8f * reach * reach) {
            continue;
        }
        points.push_back(frame.lifted(p));
    }

    tree_skeleton grown;
    std::vector<std::vector<int>> children;
    std::vector<float> trunk_left;  // how far along a branch a node is past its trunk's top
    const auto add = [&](const glm::vec3& at, const int parent, const float past) {
        grown.nodes.push_back({at, parent, 0.0f});
        children.emplace_back();
        trunk_left.push_back(past);
        if (parent >= 0) {
            children[static_cast<std::size_t>(parent)].push_back(static_cast<int>(grown.nodes.size() - 1));
        }
        return static_cast<int>(grown.nodes.size() - 1);
    };

    // The stems: up to where the trunk gives way to branches (a spruce's to its top).
    const int stem_count = std::max(1, look.stems);
    const float trunk_top = (crown_base + look.apical_dominance * (crown_top - crown_base)) * frame.aspect;
    for (int i = 0; i < stem_count; ++i) {
        const float azimuth = tau * (static_cast<float>(i) + dice.between(-0.3f, 0.3f)) / static_cast<float>(stem_count);
        const glm::vec3 out(std::cos(azimuth), 0.0f, std::sin(azimuth));
        const float spread = stem_count > 1 ? 1.0f : 0.0f;
        glm::vec3 at = out * (spread * dice.between(0.05f, 0.25f));
        int node = add(at, -1, 0.0f);
        const float lean = spread * dice.between(0.15f, 0.35f);
        while (at.y + step < trunk_top) {
            const glm::vec3 wobble(dice.between(-0.04f, 0.04f), 0.0f, dice.between(-0.04f, 0.04f));
            at += glm::normalize(up + out * lean + wobble) * step;
            node = add(at, node, 0.0f);
        }
    }

    // The branches grow toward the points nearest them until each is reached.
    const std::size_t point_count = points.size();
    std::vector<int> nearest(point_count, -1);
    std::vector<float> nearest_distance(point_count, std::numeric_limits<float>::max());
    std::vector<char> alive(point_count, 1);
    std::size_t compared = 0;
    for (int round = 0; round < most_steps; ++round) {
        const std::size_t node_count = grown.nodes.size();
        for (std::size_t p = 0; p < point_count; ++p) {
            if (!alive[p]) {
                continue;
            }
            for (std::size_t n = compared; n < node_count; ++n) {
                const float distance = glm::length(points[p] - grown.nodes[n].position);
                if (distance < nearest_distance[p]) {
                    nearest_distance[p] = distance;
                    nearest[p] = static_cast<int>(n);
                }
            }
            alive[p] = nearest_distance[p] > kill ? 1 : 0;
        }
        compared = node_count;
        std::vector<glm::vec3> pull(node_count, glm::vec3(0.0f));
        for (std::size_t p = 0; p < point_count; ++p) {
            if (alive[p] && nearest_distance[p] < influence) {
                const std::size_t n = static_cast<std::size_t>(nearest[p]);
                pull[n] += (points[p] - grown.nodes[n].position) / nearest_distance[p];
            }
        }
        bool grew = false;
        for (std::size_t n = 0; n < node_count; ++n) {
            const float strength = glm::length(pull[n]);
            if (strength < 1.0e-3f) {
                continue;
            }
            const float age = std::min(trunk_left[n] / 0.6f, 1.0f);
            const glm::vec3 jitter(dice.between(-1.0f, 1.0f), dice.between(-1.0f, 1.0f), dice.between(-1.0f, 1.0f));
            const glm::vec3 way = glm::normalize(pull[n] / strength + up * (0.25f * look.light_bend) -
                                                 up * (0.25f * look.branch_droop * age) + jitter * bend_jitter);
            const glm::vec3 next = grown.nodes[n].position + way * step;
            const bool again = std::any_of(children[n].begin(), children[n].end(), [&](const int child) {
                return glm::length(grown.nodes[static_cast<std::size_t>(child)].position - next) < 0.5f * step;
            });
            if (again || !inside(next)) {
                continue;
            }
            add(next, static_cast<int>(n), trunk_left[n] + step);
            grew = true;
        }
        if (!grew) {
            break;
        }
    }

    // Branch ends beyond the clumps' share are cut back, the shortest twigs first.
    const auto is_tip = [&](const std::size_t n) { return children[n].empty() && grown.nodes[n].parent >= 0; };
    std::vector<char> kept(grown.nodes.size(), 1);
    const std::size_t end_clumps =
        static_cast<std::size_t>(std::max(1.0f, std::round(clumps_at_ends * static_cast<float>(std::max(1, look.clumps)))));
    for (;;) {
        std::size_t tips = 0;
        int shortest = -1;
        int shortest_length = std::numeric_limits<int>::max();
        for (std::size_t n = 0; n < grown.nodes.size(); ++n) {
            if (!kept[n] || !is_tip(n)) {
                continue;
            }
            ++tips;
            int length = 0;
            for (int at = static_cast<int>(n); at >= 0 && children[static_cast<std::size_t>(at)].size() <= 1 &&
                                               grown.nodes[static_cast<std::size_t>(at)].parent >= 0;
                 at = grown.nodes[static_cast<std::size_t>(at)].parent) {
                ++length;
            }
            if (length < shortest_length) {
                shortest_length = length;
                shortest = static_cast<int>(n);
            }
        }
        if (tips <= end_clumps || shortest < 0) {
            break;
        }
        int at = shortest;
        while (at >= 0 && children[static_cast<std::size_t>(at)].size() <= 1 && grown.nodes[static_cast<std::size_t>(at)].parent >= 0) {
            kept[static_cast<std::size_t>(at)] = 0;
            const int parent = grown.nodes[static_cast<std::size_t>(at)].parent;
            std::vector<int>& siblings = children[static_cast<std::size_t>(parent)];
            siblings.erase(std::remove(siblings.begin(), siblings.end(), at), siblings.end());
            if (!siblings.empty()) {
                break;
            }
            at = parent;
        }
    }
    // Compact: parents still come before their children.
    std::vector<int> moved(grown.nodes.size(), -1);
    tree_skeleton skeleton;
    std::vector<std::vector<int>> kids;
    for (std::size_t n = 0; n < grown.nodes.size(); ++n) {
        if (!kept[n]) {
            continue;
        }
        moved[n] = static_cast<int>(skeleton.nodes.size());
        tree_skeleton::node node = grown.nodes[n];
        node.parent = node.parent >= 0 ? moved[static_cast<std::size_t>(node.parent)] : -1;
        skeleton.nodes.push_back(node);
        kids.emplace_back();
        if (node.parent >= 0) {
            kids[static_cast<std::size_t>(node.parent)].push_back(moved[n]);
        }
    }

    // The pipe model, from the ends (1) down to the stems' feet, scaled to the trunk.
    const float exponent = look.pipe_exponent;
    for (std::size_t n = skeleton.nodes.size(); n-- > 0;) {
        if (kids[n].empty()) {
            skeleton.nodes[n].radius = 1.0f;
            continue;
        }
        float sum = 0.0f;
        for (const int child : kids[n]) {
            sum += std::pow(skeleton.nodes[static_cast<std::size_t>(child)].radius, exponent);
        }
        skeleton.nodes[n].radius = std::pow(sum, 1.0f / exponent);
    }
    float thickest = 0.0f;
    for (const tree_skeleton::node& node : skeleton.nodes) {
        thickest = node.parent < 0 ? std::max(thickest, node.radius) : thickest;
    }
    for (tree_skeleton::node& node : skeleton.nodes) {
        node.radius *= look.trunk_radius / std::max(thickest, 1.0e-6f);
    }

    // Stems by order: a stem goes on into its thickest child unless a sibling is
    // nearly as thick (a fork, both leaders); the rest are a finer order.
    const float fork_share = 0.55f + 0.4f * look.apical_dominance;
    std::vector<int> stem_of(skeleton.nodes.size(), -1);
    for (std::size_t n = 0; n < skeleton.nodes.size(); ++n) {
        if (skeleton.nodes[n].parent < 0) {
            stem_of[n] = static_cast<int>(skeleton.stems.size());
            skeleton.stems.push_back({{static_cast<int>(n)}, 0});
        }
        std::vector<int> order = kids[n];
        std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
            return skeleton.nodes[static_cast<std::size_t>(a)].radius > skeleton.nodes[static_cast<std::size_t>(b)].radius;
        });
        const int own = stem_of[n];
        const int own_order = skeleton.stems[static_cast<std::size_t>(own)].order;
        const bool fork = order.size() > 1 && skeleton.nodes[static_cast<std::size_t>(order[1])].radius >=
                                                  fork_share * skeleton.nodes[static_cast<std::size_t>(order[0])].radius;
        for (std::size_t k = 0; k < order.size(); ++k) {
            const int child = order[k];
            if (k == 0 && !fork) {
                skeleton.stems[static_cast<std::size_t>(own)].nodes.push_back(child);
                stem_of[static_cast<std::size_t>(child)] = own;
            } else {
                stem_of[static_cast<std::size_t>(child)] = static_cast<int>(skeleton.stems.size());
                skeleton.stems.push_back({{static_cast<int>(n), child}, std::min(own_order + 1, 2)});
            }
        }
    }
    std::stable_sort(skeleton.stems.begin(), skeleton.stems.end(),
                     [](const tree_skeleton::stem& a, const tree_skeleton::stem& b) { return a.order < b.order; });

    // Clumps at the branch ends, then along the finest branches.
    const auto direction_of = [&](const tree_skeleton::stem& stem, const std::size_t k) {
        const glm::vec3 d = skeleton.nodes[static_cast<std::size_t>(stem.nodes[k])].position -
                            skeleton.nodes[static_cast<std::size_t>(stem.nodes[k - 1])].position;
        return glm::length(d) > 1.0e-6f ? glm::normalize(d) : up;
    };
    std::vector<std::pair<const tree_skeleton::stem*, std::size_t>> sides;
    for (const tree_skeleton::stem& stem : skeleton.stems) {
        const std::size_t last = stem.nodes.size() - 1;
        if (stem.nodes.size() > 1 && kids[static_cast<std::size_t>(stem.nodes[last])].empty()) {
            skeleton.clumps.push_back({skeleton.nodes[static_cast<std::size_t>(stem.nodes[last])].position, direction_of(stem, last)});
        }
        for (std::size_t k = 1; k + 1 < stem.nodes.size(); ++k) {
            if (stem.order == 2 || (stem.order == 1 && skeleton.stems.back().order < 2)) {
                sides.emplace_back(&stem, k);
            }
        }
    }
    const std::size_t extra = static_cast<std::size_t>(std::max(0, look.clumps - static_cast<int>(skeleton.clumps.size())));
    for (std::size_t i = 0; i < extra && !sides.empty(); ++i) {
        const auto& [stem, k] = sides[(i * sides.size()) / extra];
        const glm::vec3 along = direction_of(*stem, k);
        const glm::vec3 across = glm::cross(along, up);
        const glm::vec3 side = glm::length(across) > 1.0e-4f ? glm::normalize(across) : glm::vec3(1.0f, 0.0f, 0.0f);
        const float hand = (i % 2 == 0) ? 1.0f : -1.0f;
        skeleton.clumps.push_back({skeleton.nodes[static_cast<std::size_t>(stem->nodes[k])].position,
                                   glm::normalize(along * 0.6f + side * (0.8f * hand) + up * dice.between(-0.2f, 0.3f))});
    }
    if (skeleton.clumps.empty() && !skeleton.nodes.empty()) {
        skeleton.clumps.push_back({skeleton.nodes.back().position, up});
    }
    return skeleton;
}
}

tree_box crown_shape_box(const tree_look& look) {
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

tree_box tree_crown_box(const tree_look& look) {
    const tree_box box = crown_shape_box(look);
    const float low = std::max(box.centre.y - box.half.y, 0.0f);
    const float high = box.centre.y + box.half.y;
    return {glm::vec3(box.centre.x, 0.5f * (low + high), box.centre.z), glm::vec3(box.half.x, 0.5f * (high - low), box.half.z)};
}

float crown_reach(const tree_look& look, const glm::vec3& point) {
    const tree_box box = crown_shape_box(look);
    const glm::vec3 d = (point - box.centre) / box.half;
    if (look.shape == crown_shape::cone) {
        // Widest at the crown's foot, a point at the tree's top.
        const float base = box.centre.y - box.half.y;
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

tree_skeleton grow_skeleton(const tree_look& look, const std::uint32_t seed) {
    tree_dice dice(seed);
    const tree_true_frame frame{std::max(look.aspect, 0.1f)};
    tree_skeleton skeleton = grow_true(look, dice, frame);
    for (tree_skeleton::node& node : skeleton.nodes) {
        node.position = frame.unit(node.position);
    }
    for (tree_skeleton::clump& clump : skeleton.clumps) {
        clump.base = frame.unit(clump.base);
        clump.direction = glm::normalize(frame.unit(clump.direction));
    }
    return skeleton;
}
