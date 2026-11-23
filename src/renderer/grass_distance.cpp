#include "renderer/grass_distance.h"

#include <algorithm>
#include <array>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "renderer/grass_field.h"

namespace {
constexpr std::size_t compact_batch = 1u << 18;

float edge_distance_squared(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1.0e-12f), 0.0f, 1.0f);
    const glm::vec2 delta = p - a - t * ab;
    return glm::dot(delta, delta);
}

float box_distance(const glm::vec2& low, const glm::vec2& high, const glm::vec2& p) {
    const auto d = glm::max(glm::max(low - p, p - high), glm::vec2(0));
    return glm::dot(d, d);
}
}

void grass_bare_distance::add(const glm::vec2& a, const glm::vec2& b) {
    if (a == b) { return; }
    edges_.push_back(a.x < b.x || (a.x == b.x && a.y < b.y) ? edge{a, b} : edge{b, a});
    // Repeats are dropped as they come, so the list never holds a course's every triangle edge.
    if (edges_.size() - sorted_ >= compact_batch) { compact(); }
}

void grass_bare_distance::compact() {
    peak_ = std::max(peak_, bytes());
    const auto key = [](const edge& e) { return std::array<float, 4>{e.a.x, e.a.y, e.b.x, e.b.y}; };
    const auto before = [&](const edge& a, const edge& b) { return key(a) < key(b); };
    const auto same = [](const edge& a, const edge& b) { return a.a == b.a && a.b == b.b; };
    const auto middle = edges_.begin() + static_cast<std::ptrdiff_t>(sorted_);
    std::sort(middle, edges_.end(), before);
    std::inplace_merge(edges_.begin(), middle, edges_.end(), before);
    edges_.erase(std::unique(edges_.begin(), edges_.end(), same), edges_.end());
    sorted_ = edges_.size();
    peak_ = std::max(peak_, bytes());
}

void grass_bare_distance::build() {
    compact();
    edges_.shrink_to_fit();
    nodes_.clear();
    if (!edges_.empty()) { branch(0, static_cast<int>(edges_.size())); }
    nodes_.shrink_to_fit();
    peak_ = std::max(peak_, bytes());
}

std::size_t grass_bare_distance::bytes() const { return edges_.capacity() * sizeof(edge) + nodes_.capacity() * sizeof(node); }

float grass_bare_distance::squared(const glm::vec2& p) const {
    const float cap = grass_field_distance_cap * grass_field_distance_cap;
    return nodes_.empty() ? cap : nearest(0, p, cap);
}

int grass_bare_distance::branch(const int first, const int count) {
    node n;
    n.low = glm::min(edges_[first].a, edges_[first].b);
    n.high = glm::max(edges_[first].a, edges_[first].b);
    for (int i = first + 1; i < first + count; ++i) {
        n.low = glm::min(n.low, glm::min(edges_[i].a, edges_[i].b));
        n.high = glm::max(n.high, glm::max(edges_[i].a, edges_[i].b));
    }
    const int id = static_cast<int>(nodes_.size());
    nodes_.push_back(n);
    if (count <= 8) {
        n.first = first;
        n.count = count;
    } else {
        const int axis = n.high.x - n.low.x > n.high.y - n.low.y ? 0 : 1;
        const int half = count / 2;
        std::nth_element(edges_.begin() + first, edges_.begin() + first + half, edges_.begin() + first + count,
                         [axis](const edge& a, const edge& b) { return a.a[axis] + a.b[axis] < b.a[axis] + b.b[axis]; });
        n.left = branch(first, half);
        n.right = branch(first + half, count - half);
    }
    nodes_[id] = n;
    return id;
}

float grass_bare_distance::nearest(const int id, const glm::vec2& p, float best) const {
    const node& n = nodes_[id];
    if (box_distance(n.low, n.high, p) >= best) { return best; }
    if (n.count) {
        for (int i = n.first; i < n.first + n.count; ++i) { best = std::min(best, edge_distance_squared(p, edges_[i].a, edges_[i].b)); }
        return best;
    }
    const auto& left = nodes_[n.left];
    const auto& right = nodes_[n.right];
    const bool left_first = box_distance(left.low, left.high, p) < box_distance(right.low, right.high, p);
    best = nearest(left_first ? n.left : n.right, p, best);
    return nearest(left_first ? n.right : n.left, p, best);
}
