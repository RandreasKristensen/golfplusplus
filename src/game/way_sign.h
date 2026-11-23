#pragma once

// The signs that show people on foot the way to the next tee. A way sign is
// one post with fingerpost boards on it: each board's blunt end at the post,
// its pointed end the way to go, written on both faces so it reads from every
// side. Two kinds, placed at runtime from the holes and the paths
// (play_area::cart_paths); their posts and boards stop shots:
//   - A green sign: one board on a post just past each green but the last, on
//     the far side from where the hole is played, pointing straight at the
//     next hole's tee. Out of play, on rough, well away from every tee box and
//     clear of the paths.
//   - A junction sign: one post at a path junction, in a corner between its
//     branches, with a board for each branch that leads to a tee, pointing
//     along that branch and listing the tees it leads to. The branches are
//     those the walk from each green to the next tee takes; a branch that
//     leads to no tee gets no board, so none points back at a green or into a
//     dead end. Boards stack, the lowest tee on top. A junction with no corner
//     out of play and clear of the paths gets no sign.

#include "game/game_tuning.h"
#include "game/hole_data.h"

#include <cstddef>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct play_area;

struct way_board {
    std::vector<std::size_t> to_holes;  // ascending, indexes into play_area::holes: the tees it points the way to
    glm::vec3 points{0.0f, 0.0f, 1.0f};       // horizontal unit vector: the way to go
    glm::vec3 face_normal{1.0f, 0.0f, 0.0f};  // horizontal unit vector square to `points`: the front face's side
    glm::vec3 center{0.0f};
    float half_width = 0.0f;  // along `points`, from the post's axis to where the pointed end starts; its label sets it
};

struct way_sign {
    glm::vec3 post_foot{0.0f};
    float post_radius = 0.0f;
    float board_half_height = 0.0f;
    float board_thickness = 0.0f;
    std::vector<way_board> boards;  // stacked, top first
};

// The signs for `holes` (placed in `area`'s coordinates, its cart paths laid),
// `path_clearance` clear of every path where there is room.
std::vector<way_sign> place_way_signs(const play_area& area, const std::vector<hole_data>& holes,
                                      const way_sign_tuning& tuning, float path_clearance);

// What a board says: the course numbers (from 1) of the holes whose tees it
// points the way to, "2  7".
std::string way_sign_label(const std::vector<std::size_t>& to_holes);

// How far a board saying `label` reaches from the post's axis to its pointed
// end: every board's lettering is the same size, so the board grows with it.
float way_board_length(const way_sign_tuning& tuning, const std::string& label);

// Where a board's pointed end reaches, along its `points` from the post.
glm::vec3 way_board_tip(const way_sign& sign, const way_board& board);

// The top of the top board.
float way_sign_top(const way_sign& sign);
