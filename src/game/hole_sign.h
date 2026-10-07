#pragma once

// The sign at every hole's tee: a board on two posts on the tee box, at its
// right edge looking down the hole, standing along the hole so its face looks across
// the tee. Placed at runtime from the hole and its place on the course (no
// authored data), so every imported course has them. Pure: the server builds
// the same signs, and their posts stop shots like tree trunks
// (play_area::sign_posts). What is drawn on the face is the renderer's
// (renderer/hole_sign_face.h).

#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "physics/material_zone.h"
#include "physics/tree_collision.h"

#include <array>
#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

struct play_area;

struct hole_sign {
    std::size_t area_hole = 0;  // index into play_area::holes of the hole it is for
    // Tee, the hole's interior control points, pin, in area coordinates.
    std::vector<glm::vec3> line_of_play;
    float length = 0.0f;      // along line_of_play, horizontal, world units
    float half_width = 0.0f;  // the hole's fairway plus rough, halved
    // The hole's own greens, bunkers and water, in area coordinates: what its
    // face's map shows with the hole's ribbon, and nothing of other holes.
    std::vector<material_zone> zones;
    // Horizontal unit vectors: the hole's direction at the tee, and the way
    // the face looks (to the left of down_hole, across the tee).
    glm::vec3 down_hole{0.0f, 0.0f, 1.0f};
    glm::vec3 face_normal{1.0f, 0.0f, 0.0f};
    glm::vec3 board_center{0.0f};
    float board_half_width = 0.0f;  // along down_hole
    float board_half_height = 0.0f;
    float board_thickness = 0.0f;
    // Each post's foot on the ground, the one nearer the tee first. Posts
    // stand behind the board and reach up to its top edge.
    std::array<glm::vec3, 2> post_feet{};
    float post_radius = 0.0f;
};

// Tee, the interior control points, pin (horizontal path for the length).
std::vector<glm::vec3> line_of_play(const hole_data& hole);
// Horizontal length of a polyline.
float polyline_length(const std::vector<glm::vec3>& points);
// The point `distance` along `points` (horizontally), clamped to its ends.
glm::vec3 point_along(const std::vector<glm::vec3>& points, float distance);

// The sign for `hole` (already placed in `area`'s coordinates, its tee box
// already in `area.tee_boxes[area_hole]`), standing on the box's tiles just
// inside its right edge.
hole_sign place_hole_sign(const play_area& area, const hole_data& hole, std::size_t area_hole, const hole_sign_tuning& tuning);
std::vector<hole_sign> place_hole_signs(const play_area& area,
                                        const std::vector<hole_data>& holes,
                                        const hole_sign_tuning& tuning);

// The hole's direction at its tee: towards its line of play `look_ahead` down
// it (horizontal unit vector). Its sign and tee box face this way.
glm::vec3 down_hole_at_tee(const hole_data& hole, float look_ahead);

// The signs' posts as obstacles: trunks without leaves.
std::vector<tree_body> hole_sign_posts(const std::vector<hole_sign>& signs);
// Where a post's top is (the board's top edge).
float hole_sign_top(const hole_sign& sign);
