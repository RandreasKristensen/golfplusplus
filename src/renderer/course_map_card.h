#pragma once

// What makes the paper course map read like a course card: a scale bar and a
// north arrow, each on a paper chip in a corner of the paper. The north arrow turns with
// the map, so it points true. GL-free.

#include "game/lighting_loader.h"
#include "game/text_assets.h"
#include "renderer/course_map_layout.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_data.h"
#include "renderer/ui_rect.h"

#include <vector>

// The scale bar's length in metres: the longest of 10, 20, 50, 100, 200,
// 500, 1000, ... m that is at most a quarter of the paper's width on screen.
int course_map_scale_meters(const course_map_layout& layout, overlay_grid grid, float meters_per_unit);

// The boxes the chips take, which the hole numbers keep clear of.
std::vector<ui_rect> course_map_card_boxes(const course_map_layout& layout, overlay_grid grid, const render_data& data);

// The chips with their writing and marks, in the paper's corners.
void draw_course_map_card(overlay_batch& batch,
                          const text_assets& text,
                          const course_map_layout& layout,
                          const render_data& data,
                          const map_inks& inks);
