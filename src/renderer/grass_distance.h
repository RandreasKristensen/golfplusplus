#pragma once

// The course's bare-ground outlines as one edge index: the distance from a
// point to the nearest bare triangle edge, capped at grass_field_distance_cap.
// Outside a union of bare triangles its nearest edge is on the union's
// boundary, so shared and overlapping edges are harmless. GL-free.

#include <cstddef>
#include <vector>

#include <glm/vec2.hpp>

class grass_bare_distance {
public:
    void add(const glm::vec2& a, const glm::vec2& b);
    // Sorts and indexes the edges added; add nothing afterwards.
    void build();
    std::size_t bytes() const;
    // The most bytes the index held while it was built.
    std::size_t peak_bytes() const { return peak_; }
    // The squared distance to the nearest edge, or the cap's square.
    float squared(const glm::vec2& p) const;

private:
    struct edge {
        glm::vec2 a, b;
    };
    struct node {
        glm::vec2 low{0}, high{0};
        int first = 0, count = 0, left = 0, right = 0;
    };
    // Sorts the edges added since the last call and merges them, without repeats, into the rest.
    void compact();
    int branch(int first, int count);
    float nearest(int id, const glm::vec2& p, float best) const;
    std::vector<edge> edges_;
    std::vector<node> nodes_;
    std::size_t sorted_ = 0;  // edges_ before this are sorted and unique
    std::size_t peak_ = 0;
};
