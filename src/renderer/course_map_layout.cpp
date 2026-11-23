#include "renderer/course_map_layout.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/common.hpp>

namespace {
constexpr double pi = 3.14159265358979323846;
// The paper's most half width in clip units (of a target's half width 1).
constexpr float paper_most_half_width = 0.94f;
// Between the paper's edge and the course on it, as a share of the paper's
// half height, the same on screen both ways.
constexpr float map_margin_share = 0.08f;

// How strongly the creases through the paper's middle, and its worn edges, darken it.
constexpr float crease_alpha = 0.14f;
constexpr float edge_alpha = 0.22f;

// The paper's half size in clip units on a target of `per_clip` pixels per clip unit.
glm::vec2 paper_half_size(const glm::vec2 per_clip) {
    const float half_width = course_map_paper_half_height * course_map_paper_aspect * per_clip.y / per_clip.x;
    const float shrink = std::min(1.0f, paper_most_half_width / half_width);
    return glm::vec2(half_width, course_map_paper_half_height) * shrink;
}

// A world position on the paper's axes before scaling: world +X left, +Z
// up, turned by `turn`.
glm::vec2 turned_point(const glm::vec3& position, const glm::vec2 turn) {
    const glm::vec2 point(-position.x, position.z);
    return glm::vec2(point.x * turn.x - point.y * turn.y, point.x * turn.y + point.y * turn.x);
}

// The extent of `outline` on the paper's axes when turned by `degrees`,
// at least a world unit each way.
glm::dvec2 turned_extent(const std::vector<glm::vec3>& outline, const double degrees) {
    const double radians = degrees * pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    glm::dvec2 low(std::numeric_limits<double>::max());
    glm::dvec2 high(std::numeric_limits<double>::lowest());
    for (const glm::vec3& position : outline) {
        const glm::dvec2 point(-static_cast<double>(position.x), static_cast<double>(position.z));
        const glm::dvec2 turned(point.x * cosine - point.y * sine, point.x * sine + point.y * cosine);
        low = glm::min(low, turned);
        high = glm::max(high, turned);
    }
    return glm::max(high - low, glm::dvec2(1.0));
}
}

double course_map_fit_scale(const std::vector<glm::vec3>& outline, const double degrees, const glm::vec2 drawable) {
    const glm::dvec2 extent = turned_extent(outline, degrees);
    return std::min(static_cast<double>(drawable.x) / extent.x, static_cast<double>(drawable.y) / extent.y);
}

double course_map_turn_degrees(const std::vector<glm::vec3>& outline, const glm::vec2 drawable) {
    // The largest scale wins; scales equal but for rounding go to the smaller
    // turn, and to the counter-clockwise one of two equal turns.
    struct choice {
        int tenths = 0;  // of a degree
        double scale = 0.0;
    };
    const auto of = [&](const int tenths) { return choice{tenths, course_map_fit_scale(outline, tenths * 0.1, drawable)}; };
    choice best = of(0);
    const auto consider = [&](const int tenths) {
        const choice candidate = of(tenths);
        const double tolerance = best.scale * 1.0e-9;
        const bool beats = candidate.scale > best.scale + tolerance ||
                           (candidate.scale >= best.scale - tolerance &&
                            (std::abs(tenths) != std::abs(best.tenths) ? std::abs(tenths) < std::abs(best.tenths) : tenths > best.tenths));
        best = beats ? candidate : best;
    };
    // Every whole degree from -89 to 90, then every tenth within a degree of the best.
    for (int degrees = -89; degrees <= 90; ++degrees) {
        consider(degrees * 10);
    }
    const int coarse = best.tenths;
    for (int offset = -9; offset <= 9; ++offset) {
        consider(coarse + offset);
    }
    return best.tenths * 0.1;
}

glm::vec2 pixels_per_clip(const overlay_grid grid) {
    if (grid.width <= 0 || grid.height <= 0) {
        return glm::vec2(1.0f);
    }
    return glm::vec2(static_cast<float>(grid.width), static_cast<float>(grid.height)) * 0.5f;
}

glm::vec2 course_map_drawable(const overlay_grid grid) {
    const glm::vec2 per_clip = pixels_per_clip(grid);
    const glm::vec2 half_size = paper_half_size(per_clip);
    const glm::vec2 margin = glm::vec2(map_margin_share * half_size.y * per_clip.y) / per_clip;
    return (half_size - margin) * 2.0f * per_clip;
}

course_map_layout make_course_map_layout(const std::vector<glm::vec3>& outline, const glm::vec3& fallback, const overlay_grid grid) {
    course_map_layout layout;
    const glm::vec2 per_clip = pixels_per_clip(grid);
    layout.half_size = paper_half_size(per_clip);
    const glm::vec2 drawable = course_map_drawable(grid);
    if (outline.size() >= 2) {
        const double radians = course_map_turn_degrees(outline, drawable) * pi / 180.0;
        layout.turn = glm::vec2(static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians)));
    }

    // Only the course: the map never moves while I walk, and what leaves it
    // is shown by an arrow at its edge.
    glm::vec2 low = turned_point(outline.empty() ? fallback : outline.front(), layout.turn);
    glm::vec2 high = low;
    for (const glm::vec3& position : outline) {
        const glm::vec2 point = turned_point(position, layout.turn);
        low = glm::min(low, point);
        high = glm::max(high, point);
    }

    const glm::vec2 span = glm::max(high - low, glm::vec2(1.0f));
    const float pixels_per_unit = std::min(drawable.x / span.x, drawable.y / span.y);
    // The middle of the box turned back to the world.
    const glm::vec2 middle = (low + high) * 0.5f;
    const glm::vec2 unturned(middle.x * layout.turn.x + middle.y * layout.turn.y,
                             -middle.x * layout.turn.y + middle.y * layout.turn.x);
    layout.world_center = glm::vec3(-unturned.x, 0.0f, unturned.y);
    layout.scale = glm::vec2(pixels_per_unit) / per_clip;
    return layout;
}

glm::vec2 map_point(const course_map_layout& layout, const glm::vec3& position) {
    const glm::vec2 delta(layout.world_center.x - position.x, position.z - layout.world_center.z);
    const glm::vec2 turned(delta.x * layout.turn.x - delta.y * layout.turn.y,
                           delta.x * layout.turn.y + delta.y * layout.turn.x);
    return layout.center + turned * layout.scale;
}

glm::vec3 map_world_point(const course_map_layout& layout, const glm::vec2 clip) {
    const glm::vec2 turned = (clip - layout.center) / layout.scale;
    const glm::vec2 delta(turned.x * layout.turn.x + turned.y * layout.turn.y,
                          -turned.x * layout.turn.y + turned.y * layout.turn.x);
    return glm::vec3(layout.world_center.x - delta.x, 0.0f, layout.world_center.z + delta.y);
}

glm::vec2 course_map_north(const course_map_layout& layout) {
    const glm::vec2 delta(0.0f, -1.0f);  // world -Z, on the unturned paper
    return glm::vec2(delta.x * layout.turn.x - delta.y * layout.turn.y, delta.x * layout.turn.y + delta.y * layout.turn.x);
}

void draw_course_map_paper(overlay_batch& batch, const course_map_layout& layout, const map_inks& inks) {
    draw_overlay_quad(batch, layout.center + glm::vec2(0.035f, -0.035f), layout.half_size, inks.paper_shadow, 0.42f);
    draw_overlay_quad(batch, layout.center, layout.half_size, inks.paper, 0.96f);
}

void draw_course_map_folds(overlay_batch& batch, const course_map_layout& layout, const map_inks& inks) {
    // The creases through the middle, two pixels wide either way, and only
    // darkening what they cross, so they never tint the ground.
    const glm::vec2 pixel = glm::vec2(1.0f) / pixels_per_clip(batch.grid);
    draw_overlay_quad(batch, layout.center, glm::vec2(layout.half_size.x, pixel.y), inks.fold, crease_alpha);
    draw_overlay_quad(batch, layout.center, glm::vec2(pixel.x, layout.half_size.y), inks.fold, crease_alpha);
    for (const float side : {1.0f, -1.0f}) {
        draw_overlay_quad(batch, layout.center + glm::vec2(0.0f, side * layout.half_size.y),
                          glm::vec2(layout.half_size.x, 0.012f), inks.fold, edge_alpha);
        draw_overlay_quad(batch, layout.center + glm::vec2(side * layout.half_size.x, 0.0f),
                          glm::vec2(0.012f, layout.half_size.y), inks.fold, edge_alpha);
    }
}
