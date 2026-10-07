#pragma once

// The paper course map held up while walking (render_data::show_course_map):
// the layout that fits the course's holes onto the paper, the paper itself,
// and what is marked over the ground (trees, tee, ball, player, pin, and
// every hole's number by its tee). The ground under the marks is
// course_map_fill.h, kept in its own retained buffer by the renderer, which
// draws it between the paper and the marks. GL-free.

#include "game/text_assets.h"
#include "renderer/course_map_fill.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"
#include "renderer/ui_rect.h"

#include <vector>

// The course's holes (render_data::course_map_low/high), the player and the
// hole being played, fitted to the paper inside a small margin with world
// distances equal on screen along both axes of `grid`.
course_map_layout make_course_map_layout(const render_data& data, overlay_grid grid);

void draw_course_map_paper(overlay_batch& batch, const course_map_layout& layout);

// The boxes the holes' numbers are drawn in, one per hole in order: each
// beside its tee (above it, else below, right or left, then the same a box
// further out: whichever first clears the numbers before it), inside the
// paper.
std::vector<ui_rect> course_map_number_boxes(const course_map_layout& layout,
                                             overlay_grid grid,
                                             const std::vector<render_map_hole>& holes);

// The marks over the ground, every hole's number last so none is covered.
void draw_course_map_marks(overlay_batch& batch,
                           const text_assets& text,
                           const course_map_layout& layout,
                           const render_data& data);
