#pragma once

// Where the paper course map lies and how the course sits on it: the layout
// that fits the course's outline to the paper at the largest scale (turned to
// the angle that allows it), the paper itself, the world <-> paper mapping
// and the way north points on it. The mapping relative to the paper is the
// same at every window size (the paper is always the same shape), so a sheet
// built once (course_map_sheet.h) fits the paper at any window. GL-free.

#include "game/lighting_loader.h"
#include "renderer/overlay_batch.h"

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct course_map_layout {
    glm::vec2 center{0.0f, 0.02f};
    glm::vec2 half_size{0.68f, 0.76f};
    glm::vec3 world_center{0.0f};
    // The map turned about world_center (cosine, sine; counter-clockwise on
    // the paper), the turn at which the course fits the paper largest.
    glm::vec2 turn{1.0f, 0.0f};
    // Clip units per world unit along x and y: equal on screen, so different
    // in clip units unless the target is square.
    glm::vec2 scale{0.01f};
};

// The paper is this much wider than tall on any window, so the land it shows
// round the holes is known when a course is imported (map_paper_aspect in
// tooling/osm_import/osm_ground.py).
inline constexpr float course_map_paper_aspect = 1.5f;
// The paper's half height in clip units (the target's, always the same).
inline constexpr float course_map_paper_half_height = 0.76f;
// Clip units between the paper's edge and the ground printed on it.
inline constexpr float course_map_inset = 0.025f;

// The pixels of the paper inside its margin, where the course is fitted.
glm::vec2 course_map_drawable(overlay_grid grid);
// The scale (pixels per world unit) at which `outline`, turned `degrees`
// counter-clockwise on the paper, just fits `drawable` pixels.
double course_map_fit_scale(const std::vector<glm::vec3>& outline, double degrees, glm::vec2 drawable);
// The turn (degrees, -89 to 91) at which `outline` fits `drawable` at the
// largest scale: the best whole degree, refined to a tenth; the smaller turn
// on a tie. The importer chooses the same (map_turn in
// tooling/osm_import/osm_ground.py).
double course_map_turn_degrees(const std::vector<glm::vec3>& outline, glm::vec2 drawable);

// Target pixels per clip unit (a square grid when there is none).
glm::vec2 pixels_per_clip(overlay_grid grid);

// `outline` (the course's XZ outline; fewer than two points: a point at
// `fallback`) fitted to the paper inside a small margin, with world distances
// equal on screen along both axes of `grid`.
course_map_layout make_course_map_layout(const std::vector<glm::vec3>& outline, const glm::vec3& fallback, overlay_grid grid);

// World position -> overlay clip space. Unturned, the paper map reads from
// the player's perspective: world +X maps left and +Z up.
glm::vec2 map_point(const course_map_layout& layout, const glm::vec3& position);
// The world point (height 0) a clip-space point on the paper lies over.
glm::vec3 map_world_point(const course_map_layout& layout, glm::vec2 clip);

// The way north (world -Z) points on the paper, a unit vector in screen
// pixels (x right, y up).
glm::vec2 course_map_north(const course_map_layout& layout);

// The sheet's shadow and the paper under the ground; the creases are
// draw_course_map_folds, over the ground.
void draw_course_map_paper(overlay_batch& batch, const course_map_layout& layout, const map_inks& inks);
void draw_course_map_folds(overlay_batch& batch, const course_map_layout& layout, const map_inks& inks);
