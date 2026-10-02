#pragma once

// Paper scorecards drawn into the overlay batch: the compact card shown
// during a round and the full-screen results after the last hole. GL-free.

#include "game/scorecard.h"
#include "game/text_assets.h"
#include "renderer/overlay_batch.h"

void draw_compact_scorecard(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard);
// The full-screen scorecard after the last hole.
void draw_course_results(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard);
