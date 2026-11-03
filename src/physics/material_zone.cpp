#include "physics/material_zone.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// The distance from `point` to the segment from `a` to `b`.
float segment_distance(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 along = b - a;
    const float length_squared = glm::dot(along, along);
    const float t = length_squared > 0.0f ? std::clamp(glm::dot(point - a, along) / length_squared, 0.0f, 1.0f) : 0.0f;
    return glm::length(point - (a + along * t));
}

bool in_box(const material_zone& zone, const glm::vec2& point, const float margin) {
    return std::abs(point.x - zone.center.x) <= zone.half_size.x + margin &&
        std::abs(point.y - zone.center.z) <= zone.half_size.y + margin;
}

bool boxes_meet(const material_zone& a, const material_zone& b) {
    return std::abs(a.center.x - b.center.x) <= a.half_size.x + b.half_size.x &&
        std::abs(a.center.z - b.center.z) <= a.half_size.y + b.half_size.y;
}

// Where along the segment from `a` to `b` (0 to 1) it crosses the segment
// from `c` to `d`, if it does.
std::optional<float> crossing_along(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d) {
    const glm::vec2 r = b - a;
    const glm::vec2 s = d - c;
    const float denominator = r.x * s.y - r.y * s.x;
    if (denominator == 0.0f) {
        return std::nullopt;
    }
    const glm::vec2 q = c - a;
    const float t = (q.x * s.y - q.y * s.x) / denominator;
    const float u = (q.x * r.y - q.y * r.x) / denominator;
    if (t <= 0.0f || t >= 1.0f || u < 0.0f || u > 1.0f) {
        return std::nullopt;
    }
    return t;
}

// Whether the segments from `a` to `b` and from `c` to `d` cross, each
// passing through the other.
bool segments_cross(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d) {
    const auto side = [](const glm::vec2& p, const glm::vec2& q, const glm::vec2& r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };
    return side(a, b, c) * side(a, b, d) < 0.0f && side(c, d, a) * side(c, d, b) < 0.0f;
}

// The closed ring's segments (segment i from point i to the next) that cross
// another, not next to it.
std::vector<std::size_t> crossing_segments(const std::vector<glm::vec2>& ring) {
    const std::size_t count = ring.size();
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i;
    }
    const auto low_x = [&](const std::size_t i) { return std::min(ring[i].x, ring[(i + 1) % count].x); };
    const auto high_x = [&](const std::size_t i) { return std::max(ring[i].x, ring[(i + 1) % count].x); };
    std::sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b) { return low_x(a) < low_x(b); });
    std::vector<std::size_t> crossing;
    for (std::size_t m = 0; m < count; ++m) {
        const std::size_t i = order[m];
        for (std::size_t n = m + 1; n < count && low_x(order[n]) <= high_x(i); ++n) {
            const std::size_t j = order[n];
            const std::size_t apart = i > j ? i - j : j - i;
            if (apart <= 1U || apart == count - 1U) {
                continue;
            }
            if (segments_cross(ring[i], ring[(i + 1) % count], ring[j], ring[(j + 1) % count])) {
                crossing.push_back(i);
                crossing.push_back(j);
            }
        }
    }
    return crossing;
}

// The point `t` (0 to 1) of the way along the centripetal Catmull–Rom span
// from p1 to p2, p0 and p3 its neighbours (Barry and Goldman's form).
glm::vec2 curve_point(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, const float t) {
    // Knots step by the square root of each chord; a floor keeps repeated points apart.
    const auto knot = [](const glm::vec2& a, const glm::vec2& b) { return std::max(std::sqrt(glm::length(b - a)), 1e-3f); };
    const float t1 = knot(p0, p1);
    const float t2 = t1 + knot(p1, p2);
    const float t3 = t2 + knot(p2, p3);
    const float u = t1 + (t2 - t1) * t;
    const auto blend = [u](const glm::vec2& a, const glm::vec2& b, const float from, const float to) {
        return (a * (to - u) + b * (u - from)) / (to - from);
    };
    const glm::vec2 a1 = blend(p0, p1, 0.0f, t1);
    const glm::vec2 a2 = blend(p1, p2, t1, t2);
    const glm::vec2 a3 = blend(p2, p3, t2, t3);
    return blend(blend(a1, a2, 0.0f, t2), blend(a2, a3, t1, t3), t1, t2);
}
}

std::vector<glm::vec2> rounded_outline(const std::vector<glm::vec2>& points, const float tolerance) {
    const std::size_t count = points.size();
    if (count < 3U || !(tolerance > 0.0f)) {
        return points;
    }
    // How many straight pieces each span takes: a piece's sag from the curve
    // falls with the square of how many there are.
    constexpr int max_pieces = 64;
    std::vector<int> pieces(count, 1);
    const auto around = [&](const std::size_t i, const std::size_t step) { return points[(i + step) % count]; };
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec2 middle = curve_point(around(i, count - 1U), points[i], around(i, 1U), around(i, 2U), 0.5f);
        const float sag = glm::length(middle - (points[i] + around(i, 1U)) * 0.5f);
        pieces[i] = std::clamp(static_cast<int>(std::ceil(std::sqrt(sag / tolerance))), 1, max_pieces);
    }
    // Straightening the spans in a crossing until none is left; the mapped
    // outline itself is the end of that, so it always stops.
    while (true) {
        std::vector<glm::vec2> ring;
        std::vector<std::size_t> span_of;
        for (std::size_t i = 0; i < count; ++i) {
            for (int k = 0; k < pieces[i]; ++k) {
                ring.push_back(k == 0 ? points[i]
                                      : curve_point(around(i, count - 1U), points[i], around(i, 1U), around(i, 2U),
                                                    static_cast<float>(k) / static_cast<float>(pieces[i])));
                span_of.push_back(i);
            }
        }
        bool straightened = false;
        for (const std::size_t segment : crossing_segments(ring)) {
            straightened = straightened || pieces[span_of[segment]] > 1;
            pieces[span_of[segment]] = 1;
        }
        if (!straightened) {
            return ring;
        }
    }
}

material_zone make_material_zone(const material_zone_type type, std::vector<glm::vec2> outline) {
    material_zone zone;
    zone.type = type;
    zone.outline = std::move(outline);
    if (zone.outline.empty()) {
        return zone;
    }
    glm::vec2 low = zone.outline.front();
    glm::vec2 high = low;
    for (const glm::vec2& point : zone.outline) {
        low = glm::min(low, point);
        high = glm::max(high, point);
    }
    zone.center = glm::vec3((low.x + high.x) * 0.5f, 0.0f, (low.y + high.y) * 0.5f);
    zone.half_size = (high - low) * 0.5f;
    return zone;
}

bool zone_contains(const material_zone& zone, const glm::vec3& position) {
    const glm::vec2 point(position.x, position.z);
    if (zone.outline.size() < 3U || !in_box(zone, point, 0.0f)) {
        return false;
    }
    // Even-odd: a ray along +x crosses the edge an odd number of times.
    bool inside = false;
    const std::size_t count = zone.outline.size();
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const glm::vec2& a = zone.outline[i];
        const glm::vec2& b = zone.outline[j];
        if ((a.y > point.y) != (b.y > point.y)) {
            const float crossing = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (point.x < crossing) {
                inside = !inside;
            }
        }
    }
    return inside;
}

float zone_edge_distance(const material_zone& zone, const glm::vec3& position) {
    if (zone.outline.empty()) {
        return std::numeric_limits<float>::max();
    }
    const glm::vec2 point(position.x, position.z);
    float nearest = std::numeric_limits<float>::max();
    const std::size_t count = zone.outline.size();
    for (std::size_t i = 0; i < count; ++i) {
        nearest = std::min(nearest, segment_distance(point, zone.outline[i], zone.outline[(i + 1) % count]));
    }
    return nearest;
}

std::optional<float> zone_depth_inside(const material_zone& zone, const glm::vec3& position) {
    if (!zone_contains(zone, position)) {
        return std::nullopt;
    }
    return zone_edge_distance(zone, position);
}

float zone_distance_outside(const material_zone& zone, const glm::vec3& position) {
    if (zone.outline.size() < 3U) {
        return std::numeric_limits<float>::max();
    }
    return zone_contains(zone, position) ? 0.0f : zone_edge_distance(zone, position);
}

std::vector<std::array<glm::vec2, 3>> zone_triangles(const material_zone& zone) {
    std::vector<std::array<glm::vec2, 3>> triangles;
    std::vector<glm::vec2> points = zone.outline;
    if (points.size() < 3U) {
        return triangles;
    }
    // Counter-clockwise, so an ear turns left.
    float twice = 0.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const glm::vec2& a = points[i];
        const glm::vec2& b = points[(i + 1) % points.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    if (twice < 0.0f) {
        std::reverse(points.begin(), points.end());
    }
    const auto cross = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    const auto inside = [&](const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
        return cross(a, b, p) >= 0.0f && cross(b, c, p) >= 0.0f && cross(c, a, p) >= 0.0f;
    };
    // Each pass cuts off one ear; a pass that finds none (a nearly
    // degenerate outline) cuts its first corner, so it always ends.
    while (points.size() > 3U) {
        const std::size_t count = points.size();
        std::size_t cut = count;
        for (std::size_t i = 0; i < count && cut == count; ++i) {
            const glm::vec2& a = points[(i + count - 1) % count];
            const glm::vec2& b = points[i];
            const glm::vec2& c = points[(i + 1) % count];
            if (cross(a, b, c) <= 0.0f) {
                continue;
            }
            bool empty = true;
            for (std::size_t j = 0; j < count && empty; ++j) {
                if (j != i && j != (i + count - 1) % count && j != (i + 1) % count && inside(points[j], a, b, c)) {
                    empty = false;
                }
            }
            if (empty) {
                cut = i;
            }
        }
        if (cut == count) {
            cut = 0;
        }
        triangles.push_back({points[(cut + count - 1) % count], points[cut], points[(cut + 1) % count]});
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(cut));
    }
    triangles.push_back({points[0], points[1], points[2]});
    return triangles;
}

std::vector<zone_edge> edge_outside(const material_zone& zone, const std::vector<const material_zone*>& others) {
    std::vector<const material_zone*> near;
    for (const material_zone* other : others) {
        if (other != &zone && other->outline.size() >= 3U && boxes_meet(zone, *other)) {
            near.push_back(other);
        }
    }
    std::vector<zone_edge> pieces;
    const std::size_t count = zone.outline.size();
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec2& a = zone.outline[i];
        const glm::vec2& b = zone.outline[(i + 1) % count];
        std::vector<float> cuts{0.0f, 1.0f};
        for (const material_zone* other : near) {
            const std::size_t other_count = other->outline.size();
            for (std::size_t k = 0; k < other_count; ++k) {
                if (const auto t = crossing_along(a, b, other->outline[k], other->outline[(k + 1) % other_count])) {
                    cuts.push_back(*t);
                }
            }
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
            const glm::vec2 from = a + (b - a) * cuts[k];
            const glm::vec2 to = a + (b - a) * cuts[k + 1];
            const glm::vec2 middle = 0.5f * (from + to);
            const bool covered = std::any_of(near.begin(), near.end(), [&](const material_zone* other) {
                return zone_contains(*other, glm::vec3(middle.x, 0.0f, middle.y));
            });
            if (!covered) {
                pieces.push_back({from, to});
            }
        }
    }
    return pieces;
}

float edges_distance(const std::vector<zone_edge>& edges, const glm::vec3& position) {
    const glm::vec2 point(position.x, position.z);
    float nearest = std::numeric_limits<float>::max();
    for (const zone_edge& edge : edges) {
        nearest = std::min(nearest, segment_distance(point, edge[0], edge[1]));
    }
    return nearest;
}

float zone_area(const material_zone& zone) {
    float twice = 0.0f;
    const std::size_t count = zone.outline.size();
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec2& a = zone.outline[i];
        const glm::vec2& b = zone.outline[(i + 1) % count];
        twice += a.x * b.y - b.x * a.y;
    }
    return std::abs(twice) * 0.5f;
}
