#pragma once

// The paper course map held up while walking (render_data::show_course_map):
// what is marked over the ground (course_map_sheet.h, which the renderer
// draws between the paper and these marks, with every pin's flag): every
// hole's tee box, the ball, player, my bag, the course card's scale bar and
// north arrow (course_map_card.h), and every hole's number in the middle of
// the hole. GL-free.

#include "game/lighting_loader.h"
#include "game/text_assets.h"
#include "renderer/course_map_layout.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"
#include "renderer/ui_rect.h"

#include <vector>

// The course's holes (render_data::course_map_outline) fitted to the paper
// inside a small margin, with world distances equal on screen along both axes
// of `grid`. It never moves while I play: a mark off it (me, my bag, the
// ball) is an arrow at its edge pointing out to it. The paper is the same
// shape on any window, so an import knows the land it shows.
course_map_layout make_course_map_layout(const render_data& data, overlay_grid grid);

// The boxes data.map_holes' numbers are drawn in, one per hole in order:
// each centred on its hole's line of play, halfway along it, or further
// towards either end while that place is taken by the other numbers or the
// marks and the course card's chips, inside the paper; all as large as lets
// no two overlap.
std::vector<ui_rect> course_map_number_boxes(const course_map_layout& layout,
                                             overlay_grid grid,
                                             const render_data& data);

// The marks over the ground, every hole's number last so none is covered.
void draw_course_map_marks(overlay_batch& batch,
                           const text_assets& text,
                           const course_map_layout& layout,
                           const render_data& data,
                           const map_inks& inks);
