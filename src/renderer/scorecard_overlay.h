#pragma once

// Paper scorecards drawn into the overlay batch: the compact card shown
// during a round, the group's card beside it online, and the full-screen
// results after the last hole. GL-free.

#include "game/scorecard.h"
#include "game/text_assets.h"
#include "renderer/overlay_batch.h"

#include <vector>

void draw_compact_scorecard(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard);
// One row per group member (name, holes played, strokes, against par);
// nothing when `rows` is empty.
void draw_group_scorecard(overlay_batch& batch, const text_assets& text, const std::vector<group_scorecard_row>& rows);
// The full-screen scorecard after the last hole.
void draw_course_results(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard);
