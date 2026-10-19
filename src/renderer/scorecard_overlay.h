#pragma once

// The paper scorecard drawn into the overlay batch: held up during a round,
// and full-screen with the results after the last hole. Like a real card,
// holes run across in blocks of up to nine (OUT and IN with subtotals on
// eighteen holes, then the total), a row of par and a row of strokes per
// player, so every hole always fits. GL-free.

#include "game/scorecard.h"
#include "game/text_assets.h"
#include "game/text_style.h"
#include "renderer/overlay_batch.h"
#include "renderer/ui_rect.h"

#include <string>
#include <vector>

// What a label on the card is. The grid's labels (all but the card's own
// lines and player names) share one text size, and the names another.
enum class scorecard_label_kind { card, heading, name, hole, par, par_total, strokes, strokes_total, relative };

struct scorecard_label {
    scorecard_label_kind kind = scorecard_label_kind::card;
    std::string text;
    text_style style;
    ui_rect box;
};

// Every label the card draws, held up (`results` false) or as the results.
std::vector<scorecard_label> scorecard_labels(const text_assets& text, const scorecard_data& scorecard, bool results);

void draw_compact_scorecard(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard);
void draw_course_results(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard);
