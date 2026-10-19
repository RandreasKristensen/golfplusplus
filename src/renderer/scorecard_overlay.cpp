#include "renderer/scorecard_overlay.h"

#include "game/text_ids.h"
#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {
constexpr std::size_t max_holes_per_block = 9;
constexpr float label_column_fraction = 0.25f;
constexpr float summary_column_weight = 1.4f;  // OUT, IN, TOT and +/- against one hole
constexpr float block_gap_rows = 0.4f;
constexpr float max_row_height = 0.12f;

const glm::vec3 grid_ink(0.23f, 0.19f, 0.13f);
const glm::vec3 current_hole_tint(0.46f, 0.16f, 0.11f);

// The most room each card takes; a card is as tall as its rows need. Held
// up, below the mode label and skills panel and left of the club label; the
// results nearly full screen.
const ui_rect held_room{glm::vec2(-0.17f, 0.10f), glm::vec2(0.78f, 0.54f)};
const ui_rect results_room{glm::vec2(0.0f), glm::vec2(0.90f, 0.86f)};

// Heights in clip units of the bands above and below the grid, top to bottom.
struct card_bands {
    float top;
    float title;
    float subtitle;
    float gap;
    float hint;
    float bottom;
};

const card_bands held_bands{0.03f, 0.10f, 0.065f, 0.035f, 0.0f, 0.045f};
const card_bands results_bands{0.03f, 0.14f, 0.085f, 0.05f, 0.08f, 0.02f};

// Where each part of the card goes; text sizes follow from these boxes.
struct card_layout {
    ui_rect title;
    ui_rect subtitle;
    ui_rect grid;
    ui_rect hint;
};

// The holes split into equal blocks of at most nine, stacked top to bottom.
// Every block has the same columns: the row labels, its holes, a subtotal
// when there is more than one block, and on the last the total and +/-.
struct card_grid {
    std::size_t hole_count = 0;
    std::size_t block_count = 1;
    std::size_t holes_per_block = 0;
    std::size_t rows_per_block = 0;  // holes, par, one per player
    std::vector<float> x_edges;
    float top = 0.0f;
    float row_height = 0.0f;
};

struct card_plan {
    ui_rect card;
    card_layout layout;
    card_grid grid;
};

bool has_subtotals(const card_grid& grid) {
    return grid.block_count > 1U;
}

std::size_t subtotal_column(const card_grid& grid) {
    return grid.holes_per_block + 1U;
}

std::size_t total_column(const card_grid& grid) {
    return subtotal_column(grid) + (has_subtotals(grid) ? 1U : 0U);
}

std::size_t relative_column(const card_grid& grid) {
    return total_column(grid) + 1U;
}

// Grid rows from the first block's top to the last one's bottom.
float grid_rows(const card_grid& grid) {
    return static_cast<float>(grid.block_count * grid.rows_per_block) + block_gap_rows * static_cast<float>(grid.block_count - 1U);
}

std::vector<float> column_edges(const card_grid& grid, const ui_rect& area) {
    const float left = rect_left(area);
    const float label_right = left + (rect_right(area) - left) * label_column_fraction;
    const std::size_t summary_columns = relative_column(grid) - grid.holes_per_block;
    const float weights = static_cast<float>(grid.holes_per_block) + summary_column_weight * static_cast<float>(summary_columns);
    const float hole_width = (rect_right(area) - label_right) / weights;
    std::vector<float> edges{left, label_right};
    for (std::size_t column = 1; column <= relative_column(grid); ++column) {
        edges.push_back(edges.back() + hole_width * (column <= grid.holes_per_block ? 1.0f : summary_column_weight));
    }
    edges.back() = rect_right(area);
    return edges;
}

card_plan plan_card(const scorecard_data& scorecard, const bool results) {
    const ui_rect& room = results ? results_room : held_room;
    const card_bands& bands = results ? results_bands : held_bands;
    card_plan plan;
    card_grid& grid = plan.grid;
    grid.hole_count = scorecard.pars.size();
    grid.block_count = std::max<std::size_t>(1U, (grid.hole_count + max_holes_per_block - 1U) / max_holes_per_block);
    grid.holes_per_block = (grid.hole_count + grid.block_count - 1U) / grid.block_count;
    grid.rows_per_block = 2U + scorecard.players.size();

    const float fixed = bands.top + bands.title + bands.subtitle + bands.gap + bands.hint + bands.bottom;
    grid.row_height = std::min(max_row_height, (room.half_size.y * 2.0f - fixed) / grid_rows(grid));
    const float height = fixed + grid.row_height * grid_rows(grid);
    // Held up from the top of its room; the results centred.
    const float top = results ? room.center.y + height * 0.5f : rect_top(room);
    plan.card = rect_from_edges(rect_left(room), top - height, rect_right(room), top);

    const ui_rect content = inset_rect(plan.card, glm::vec2(0.052f, 0.0f));
    const ui_rect title_column = slice_x(plan.card, 0.11f, 0.89f);
    float y = top - bands.top;
    const auto band = [&y](const ui_rect& column, const float band_height) {
        const ui_rect box = rect_from_edges(rect_left(column), y - band_height, rect_right(column), y);
        y -= band_height;
        return box;
    };
    plan.layout.title = band(title_column, bands.title);
    plan.layout.subtitle = band(title_column, bands.subtitle);
    y -= bands.gap;
    plan.layout.grid = band(content, grid.row_height * grid_rows(grid));
    plan.layout.hint = band(content, bands.hint);
    grid.top = rect_top(plan.layout.grid);
    grid.x_edges = column_edges(grid, content);
    return plan;
}

float block_top(const card_grid& grid, const std::size_t block) {
    return grid.top - static_cast<float>(block) * (static_cast<float>(grid.rows_per_block) + block_gap_rows) * grid.row_height;
}

std::size_t block_first_hole(const card_grid& grid, const std::size_t block) {
    return block * grid.holes_per_block;
}

std::size_t block_end_hole(const card_grid& grid, const std::size_t block) {
    return std::min(grid.hole_count, block_first_hole(grid, block) + grid.holes_per_block);
}

bool last_block(const card_grid& grid, const std::size_t block) {
    return block + 1U == grid.block_count;
}

// The columns a block draws: the total and +/- only on the last.
std::size_t block_column_count(const card_grid& grid, const std::size_t block) {
    return last_block(grid, block) ? relative_column(grid) + 1U : total_column(grid);
}

ui_rect grid_cell(const card_grid& grid, const std::size_t block, const std::size_t row, const std::size_t column) {
    const float top = block_top(grid, block) - static_cast<float>(row) * grid.row_height;
    return rect_from_edges(grid.x_edges[column], top - grid.row_height, grid.x_edges[column + 1U], top);
}

// The label column is left aligned, a little in from the grid line.
ui_rect label_cell(const card_grid& grid, const std::size_t block, const std::size_t row) {
    const ui_rect cell = grid_cell(grid, block, row, 0);
    return rect_from_edges(rect_left(cell) + 0.014f, rect_bottom(cell), rect_right(cell), rect_top(cell));
}

bool current_hole_in_block(const scorecard_data& scorecard, const card_grid& grid, const std::size_t block) {
    return !scorecard.finished && scorecard.current_hole_index >= block_first_hole(grid, block) &&
           scorecard.current_hole_index < block_end_hole(grid, block);
}

std::string count_label(const int count) {
    return count > 0 ? std::to_string(count) : "";
}

int sum_between(const std::vector<int>& values, const std::size_t first, const std::size_t end) {
    int sum = 0;
    for (std::size_t i = first; i < end; ++i) {
        sum += values[i];
    }
    return sum;
}

void add_block_labels(std::vector<scorecard_label>& labels,
                      const text_assets& text,
                      const scorecard_data& scorecard,
                      const card_grid& grid,
                      const std::size_t block) {
    const text_style& header = find_text_style(text, style_scorecard_header);
    const text_style& ink = find_text_style(text, style_scorecard_row);
    const text_style& current_ink = find_text_style(text, style_scorecard_row_current);
    const text_style& total = find_text_style(text, style_scorecard_total);
    const text_style label_header = with_align(header, text_align::left);
    const std::size_t first = block_first_hole(grid, block);
    const std::size_t end = block_end_hole(grid, block);
    const bool show_current = current_hole_in_block(scorecard, grid, block);
    const auto add = [&labels](const scorecard_label_kind kind, std::string label, const text_style& style, const ui_rect& box) {
        labels.push_back(scorecard_label{kind, std::move(label), style, box});
    };

    add(scorecard_label_kind::heading, lookup_text(text, text_scorecard_header_hole), label_header, label_cell(grid, block, 0));
    add(scorecard_label_kind::heading, lookup_text(text, text_scorecard_header_par), label_header, label_cell(grid, block, 1));
    for (std::size_t hole = first; hole < end; ++hole) {
        const std::size_t column = hole - first + 1U;
        const bool current = show_current && hole == scorecard.current_hole_index;
        add(scorecard_label_kind::hole, std::to_string(hole + 1U), current ? current_ink : header, grid_cell(grid, block, 0, column));
        add(scorecard_label_kind::par, std::to_string(scorecard.pars[hole]), ink, grid_cell(grid, block, 1, column));
    }
    if (has_subtotals(grid)) {
        const std::size_t column = subtotal_column(grid);
        add(scorecard_label_kind::heading, lookup_text(text, block == 0U ? text_scorecard_out : text_scorecard_in), header,
            grid_cell(grid, block, 0, column));
        add(scorecard_label_kind::par_total, std::to_string(sum_between(scorecard.pars, first, end)), total, grid_cell(grid, block, 1, column));
    }
    const bool totals = last_block(grid, block);
    if (totals) {
        add(scorecard_label_kind::heading, lookup_text(text, text_scorecard_total), header, grid_cell(grid, block, 0, total_column(grid)));
        add(scorecard_label_kind::heading, lookup_text(text, text_scorecard_header_relative), header,
            grid_cell(grid, block, 0, relative_column(grid)));
        add(scorecard_label_kind::par_total, std::to_string(sum_between(scorecard.pars, 0, grid.hole_count)), total,
            grid_cell(grid, block, 1, total_column(grid)));
    }

    for (std::size_t i = 0; i < scorecard.players.size(); ++i) {
        const scorecard_player& player = scorecard.players[i];
        const std::size_t row = i + 2U;
        const text_style& row_ink = player.me ? current_ink : ink;
        add(scorecard_label_kind::name, player.name, with_align(row_ink, text_align::left), label_cell(grid, block, row));
        for (std::size_t hole = first; hole < end; ++hole) {
            add(scorecard_label_kind::strokes, count_label(player.strokes[hole]), row_ink, grid_cell(grid, block, row, hole - first + 1U));
        }
        if (has_subtotals(grid)) {
            add(scorecard_label_kind::strokes_total, count_label(sum_between(player.strokes, first, end)), total,
                grid_cell(grid, block, row, subtotal_column(grid)));
        }
        if (totals && player.total_strokes > 0) {
            add(scorecard_label_kind::strokes_total, std::to_string(player.total_strokes), total, grid_cell(grid, block, row, total_column(grid)));
            add(scorecard_label_kind::relative, player.relative_label, row_ink, grid_cell(grid, block, row, relative_column(grid)));
        }
    }
}

std::vector<scorecard_label> card_labels(const text_assets& text, const scorecard_data& scorecard, const card_plan& plan, const bool results) {
    std::vector<scorecard_label> labels;
    if (scorecard.pars.empty()) {
        return labels;
    }
    const card_layout& layout = plan.layout;
    labels.push_back(scorecard_label{scorecard_label_kind::card, scorecard.course_name, find_text_style(text, style_scorecard_title), layout.title});
    labels.push_back(scorecard_label{scorecard_label_kind::card,
                                     lookup_text(text, results ? text_scorecard_results_title : text_scorecard_title),
                                     find_text_style(text, style_scorecard_subtitle), layout.subtitle});
    for (std::size_t block = 0; block < plan.grid.block_count; ++block) {
        add_block_labels(labels, text, scorecard, plan.grid, block);
    }
    if (results) {
        labels.push_back(scorecard_label{scorecard_label_kind::card,
                                         lookup_text(text, scorecard.next_round ? text_scorecard_results_hint_online : text_scorecard_results_hint),
                                         find_text_style(text, style_scorecard_hint), layout.hint});
    }
    return labels;
}

// Labels drawn at one size: the grid's, and the names'. The card's own
// lines each have their own.
enum class label_group { card, grid, names };

label_group group_of(const scorecard_label& label) {
    switch (label.kind) {
        case scorecard_label_kind::card: return label_group::card;
        case scorecard_label_kind::name: return label_group::names;
        default: return label_group::grid;
    }
}

// The largest scale every label of `group` fits at; 0 when one doesn't fit.
int shared_scale(const std::vector<scorecard_label>& labels, const label_group group, const pixel_font_data& font, const overlay_grid& grid) {
    int scale = std::numeric_limits<int>::max();
    for (const scorecard_label& label : labels) {
        if (group_of(label) == group && !label.text.empty()) {
            scale = std::min(scale, fit_text_scale(font, label.style, label.text, label.box, grid));
        }
    }
    return scale;
}

void draw_paper_card_base(overlay_batch& batch, const ui_rect& card) {
    const glm::vec3 paper(0.78f, 0.73f, 0.56f);
    const glm::vec3 paper_shadow(0.12f, 0.095f, 0.065f);
    const glm::vec3 fold(0.42f, 0.35f, 0.24f);

    draw_overlay_quad(batch, card.center + glm::vec2(0.030f, -0.034f), card.half_size, paper_shadow, 0.40f);
    draw_overlay_quad(batch, card.center, card.half_size, paper, 0.97f);
    draw_overlay_quad(batch, card.center + glm::vec2(-card.half_size.x * 0.28f, 0.0f), glm::vec2(0.004f, card.half_size.y), fold, 0.14f);
    draw_overlay_quad(batch, card.center + glm::vec2(card.half_size.x * 0.22f, 0.0f), glm::vec2(0.003f, card.half_size.y), fold, 0.10f);
    draw_button_outline(batch, card.center, card.half_size, fold, 0.44f);
    draw_button_outline(batch, card.center, card.half_size, grid_ink, 0.22f);
}

// The current hole's column tinted, then the block's lines.
void draw_block_marks(overlay_batch& batch, const scorecard_data& scorecard, const card_grid& grid, const std::size_t block) {
    if (current_hole_in_block(scorecard, grid, block)) {
        const std::size_t column = scorecard.current_hole_index - block_first_hole(grid, block) + 1U;
        const ui_rect top = grid_cell(grid, block, 0, column);
        const ui_rect bottom = grid_cell(grid, block, grid.rows_per_block - 1U, column);
        const ui_rect tint = rect_from_edges(rect_left(top), rect_bottom(bottom), rect_right(top), rect_top(top));
        draw_overlay_quad(batch, tint.center, tint.half_size, current_hole_tint, 0.14f);
    }

    const std::size_t columns = block_column_count(grid, block);
    const float top = block_top(grid, block);
    const float bottom = top - static_cast<float>(grid.rows_per_block) * grid.row_height;
    for (std::size_t edge = 0; edge <= columns; ++edge) {
        draw_overlay_segment(batch, glm::vec2(grid.x_edges[edge], top), glm::vec2(grid.x_edges[edge], bottom), 0.006f, grid_ink, 0.38f);
    }
    for (std::size_t row = 0; row <= grid.rows_per_block; ++row) {
        const float y = top - static_cast<float>(row) * grid.row_height;
        const float alpha = row == 2U ? 0.55f : 0.34f;  // par above the players
        draw_overlay_segment(batch, glm::vec2(grid.x_edges.front(), y), glm::vec2(grid.x_edges[columns], y), 0.006f, grid_ink, alpha);
    }
}

void draw_scorecard_card(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard, const bool results) {
    const card_plan plan = plan_card(scorecard, results);
    const std::vector<scorecard_label> labels = card_labels(text, scorecard, plan, results);
    if (labels.empty()) {
        return;
    }
    draw_paper_card_base(batch, plan.card);
    for (std::size_t block = 0; block < plan.grid.block_count; ++block) {
        draw_block_marks(batch, scorecard, plan.grid, block);
    }

    // When a group doesn't fit at one size, each label shrinks, and is cut
    // off, by itself. Names are never bigger than the numbers.
    const int grid_scale = shared_scale(labels, label_group::grid, text.font, batch.grid);
    int name_scale = shared_scale(labels, label_group::names, text.font, batch.grid);
    if (grid_scale > 0) {
        name_scale = std::min(name_scale, grid_scale);
    }
    for (const scorecard_label& label : labels) {
        const label_group group = group_of(label);
        const int scale = group == label_group::grid ? grid_scale : group == label_group::names ? name_scale : 0;
        if (scale > 0 && !label.text.empty()) {
            draw_text_layout(batch, text.font, layout_text_at_scale(text.font, label.style, label.text, label.box, batch.grid, scale),
                             label.style.color);
        } else {
            draw_label(batch, text.font, label.style, label.text, label.box);
        }
    }
}
}

std::vector<scorecard_label> scorecard_labels(const text_assets& text, const scorecard_data& scorecard, const bool results) {
    return card_labels(text, scorecard, plan_card(scorecard, results), results);
}

void draw_compact_scorecard(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_scorecard_card(batch, text, scorecard, false);
}

void draw_course_results(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.015f, 0.013f, 0.012f), 0.70f);
    draw_scorecard_card(batch, text, scorecard, true);
}
