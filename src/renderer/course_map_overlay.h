#pragma once

// The paper course map held up while walking (render_data::show_course_map):
// the layout that fits the course's holes onto the paper, the paper itself,
// and what is marked over the ground (trees, tee, ball, player, pin, and
// every hole's number in the middle of the hole). The ground under the marks is
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

// The boxes data.map_holes' numbers are drawn in, one per hole in order:
// each centred on its hole's line of play, halfway along it, or further
// towards either end while that place is taken by the numbers before it or
// the marks, inside the paper; all as large as lets no two overlap.
std::vector<ui_rect> course_map_number_boxes(const course_map_layout& layout,
                                             overlay_grid grid,
                                             const render_data& data);

// The marks over the ground, every hole's number last so none is covered.
void draw_course_map_marks(overlay_batch& batch,
                           const text_assets& text,
                           const course_map_layout& layout,
                           const render_data& data);
