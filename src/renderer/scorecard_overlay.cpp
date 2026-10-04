#include "renderer/scorecard_overlay.h"

#include "game/text_ids.h"
#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <algorithm>
#include <array>
#include <string>

namespace {
// Where each part of the card goes; text sizes follow from these boxes, so
// the compact card gets smaller text by being smaller.
struct card_layout {
    ui_rect title;
    ui_rect subtitle;
    ui_rect grid;
    ui_rect total;
    ui_rect hint;
};

card_layout make_card_layout(const ui_rect& card, const bool compact) {
    const ui_rect content = inset_rect(card, glm::vec2(0.052f, 0.0f));
    const ui_rect title_column = slice_x(card, 0.11f, 0.89f);
    if (compact) {
        return card_layout{slice_y(title_column, 0.03f, 0.13f),
                           slice_y(title_column, 0.13f, 0.19f),
                           slice_y(content, 0.21f, 0.86f),
                           slice_y(content, 0.87f, 0.96f),
                           ui_rect{}};
    }
    return card_layout{slice_y(title_column, 0.02f, 0.10f),
                       slice_y(title_column, 0.10f, 0.15f),
                       slice_y(content, 0.16f, 0.88f),
                       slice_y(content, 0.89f, 0.95f),
                       slice_y(content, 0.955f, 0.995f)};
}

// Header row plus one row per hole, in columns hole / par / score / +-.
struct scorecard_grid {
    std::array<float, 5> x_edges{};
    float top = 0.0f;
    float row_height = 0.0f;
    int row_count = 0;  // including the header
};

scorecard_grid make_grid(const ui_rect& area, const std::size_t hole_rows, const bool compact) {
    const float x0 = rect_left(area);
    const float x4 = rect_right(area);
    const float hole_width = compact ? (x4 - x0) * 0.28f : (x4 - x0) * 0.46f;
    const float par_width = (x4 - x0) * 0.16f;
    const float score_width = (x4 - x0) * 0.22f;

    scorecard_grid grid;
    grid.x_edges = {{x0, x0 + hole_width, x0 + hole_width + par_width, x0 + hole_width + par_width + score_width, x4}};
    grid.top = rect_top(area);
    grid.row_count = static_cast<int>(hole_rows) + 1;
    grid.row_height = std::min(compact ? 0.056f : 0.060f, area.half_size.y * 2.0f / static_cast<float>(grid.row_count));
    return grid;
}

// Row 0 is the header.
ui_rect grid_cell(const scorecard_grid& grid, const int row, const std::size_t column) {
    const float top = grid.top - static_cast<float>(row) * grid.row_height;
    return rect_from_edges(grid.x_edges[column], top - grid.row_height, grid.x_edges[column + 1], top);
}

// The same columns on a row outside the grid (the totals).
ui_rect band_cell(const scorecard_grid& grid, const ui_rect& band, const std::size_t column) {
    return rect_from_edges(grid.x_edges[column], rect_bottom(band), grid.x_edges[column + 1], rect_top(band));
}

// The hole column is left aligned, a little in from the grid line.
ui_rect hole_cell(const ui_rect& cell) {
    return rect_from_edges(rect_left(cell) + 0.014f, rect_bottom(cell), rect_right(cell), rect_top(cell));
}

std::string score_label(const scorecard_row& row) {
    return row.played ? std::to_string(row.strokes) : "";
}

std::string relative_label(const scorecard_row& row) {
    return row.played ? row.relative_label : "";
}

std::string hole_label(const text_assets& text, const scorecard_row& row, const bool compact) {
    if (compact || row.hole_name.empty()) {
        return std::to_string(row.hole_number);
    }
    return format_text(text, text_scorecard_hole_row, {{"hole", std::to_string(row.hole_number)}, {"name", row.hole_name}});
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
}

void draw_grid_lines(overlay_batch& batch, const ui_rect& card, const scorecard_grid& grid) {
    const glm::vec3 ink(0.23f, 0.19f, 0.13f);
    const float bottom = grid.top - static_cast<float>(grid.row_count) * grid.row_height;
    for (const float x : grid.x_edges) {
        draw_overlay_segment(batch, glm::vec2(x, grid.top), glm::vec2(x, bottom), 0.004f, ink, 0.38f);
    }
    for (int i = 0; i <= grid.row_count; ++i) {
        const float y = grid.top - static_cast<float>(i) * grid.row_height;
        draw_overlay_segment(batch, glm::vec2(grid.x_edges.front(), y), glm::vec2(grid.x_edges.back(), y), 0.004f, ink, 0.34f);
    }
    draw_button_outline(batch, card.center, card.half_size, ink, 0.22f);
}

void draw_headers(overlay_batch& batch, const text_assets& text, const scorecard_grid& grid) {
    const text_style& style = find_text_style(text, style_scorecard_header);
    const std::array<const char*, 4> keys{{text_scorecard_header_hole, text_scorecard_header_par, text_scorecard_header_score, text_scorecard_header_relative}};
    for (std::size_t column = 0; column < keys.size(); ++column) {
        draw_label(batch, text.font, style, lookup_text(text, keys[column]), grid_cell(grid, 0, column));
    }
}

void draw_row(overlay_batch& batch,
              const text_assets& text,
              const scorecard_row& row,
              const scorecard_grid& grid,
              const int grid_row,
              const bool compact,
              const bool current) {
    const text_style& ink = find_text_style(text, current ? style_scorecard_row_current : style_scorecard_row);
    const text_style& score_ink = row.played ? ink : find_text_style(text, style_scorecard_row_pending);
    draw_label(batch, text.font, with_align(ink, text_align::left), hole_label(text, row, compact), hole_cell(grid_cell(grid, grid_row, 0)));
    draw_label(batch, text.font, ink, std::to_string(row.par), grid_cell(grid, grid_row, 1));
    draw_label(batch, text.font, score_ink, score_label(row), grid_cell(grid, grid_row, 2));
    draw_label(batch, text.font, score_ink, relative_label(row), grid_cell(grid, grid_row, 3));
}

void draw_totals(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard, const scorecard_grid& grid, const ui_rect& band) {
    const text_style& style = find_text_style(text, style_scorecard_total);
    draw_label(batch, text.font, with_align(style, text_align::left), lookup_text(text, text_scorecard_total), hole_cell(band_cell(grid, band, 0)));
    draw_label(batch, text.font, style, std::to_string(scorecard.total_par), band_cell(grid, band, 1));
    draw_label(batch, text.font, style, std::to_string(scorecard.total_strokes), band_cell(grid, band, 2));
    draw_label(batch, text.font, style, scorecard.total_relative_label, band_cell(grid, band, 3));
}

void draw_scorecard_card(overlay_batch& batch,
                         const text_assets& text,
                         const scorecard_data& scorecard,
                         const ui_rect& card,
                         const bool compact) {
    if (scorecard.rows.empty()) {
        return;
    }

    draw_paper_card_base(batch, card);
    const card_layout layout = make_card_layout(card, compact);
    draw_label(batch, text.font, find_text_style(text, style_scorecard_title), scorecard.course_name, layout.title);
    draw_label(batch,
               text.font,
               find_text_style(text, style_scorecard_subtitle),
               lookup_text(text, compact ? text_scorecard_title : text_scorecard_results_title),
               layout.subtitle);

    const std::size_t row_limit = compact ? std::min<std::size_t>(scorecard.rows.size(), 8U) : scorecard.rows.size();
    std::size_t first_row = 0;
    if (compact && scorecard.rows.size() > row_limit) {
        const std::size_t current = std::min(scorecard.current_hole_index, scorecard.rows.size() - 1U);
        const std::size_t preferred = current > 3U ? current - 3U : 0U;
        first_row = std::min(preferred, scorecard.rows.size() - row_limit);
    }

    const scorecard_grid grid = make_grid(layout.grid, row_limit, compact);
    draw_grid_lines(batch, card, grid);
    draw_headers(batch, text, grid);
    for (std::size_t row_index = 0; row_index < row_limit; ++row_index) {
        const std::size_t source_index = first_row + row_index;
        const bool current = !scorecard.finished && source_index == scorecard.current_hole_index;
        draw_row(batch, text, scorecard.rows[source_index], grid, static_cast<int>(row_index) + 1, compact, current);
    }
    draw_totals(batch, text, scorecard, grid, layout.total);

    if (!compact) {
        draw_label(batch, text.font, find_text_style(text, style_scorecard_hint),
                   lookup_text(text, scorecard.next_round ? text_scorecard_results_hint_online : text_scorecard_results_hint), layout.hint);
    }
}
}

void draw_compact_scorecard(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_scorecard_card(batch, text, scorecard, ui_rect{glm::vec2(-0.48f, 0.32f), glm::vec2(0.44f, 0.44f)}, true);
}

void draw_group_scorecard(overlay_batch& batch, const text_assets& text, const std::vector<group_scorecard_row>& rows) {
    if (rows.empty()) {
        return;
    }
    // Right of the compact card, clear of the key icons.
    const ui_rect card{glm::vec2(0.30f, 0.24f), glm::vec2(0.34f, 0.26f)};
    draw_paper_card_base(batch, card);
    const ui_rect content = inset_rect(card, glm::vec2(0.03f, 0.0f));
    draw_label(batch, text.font, find_text_style(text, style_scorecard_title), lookup_text(text, text_scorecard_group_title),
               slice_y(content, 0.04f, 0.20f));

    const ui_rect area = slice_y(content, 0.24f, 0.96f);
    const float x0 = rect_left(area);
    const float x4 = rect_right(area);
    const float width = x4 - x0;
    scorecard_grid grid;
    grid.x_edges = {{x0, x0 + width * 0.52f, x0 + width * 0.66f, x0 + width * 0.82f, x4}};
    grid.top = rect_top(area);
    grid.row_count = static_cast<int>(rows.size()) + 1;
    grid.row_height = std::min(0.07f, area.half_size.y * 2.0f / static_cast<float>(grid.row_count));
    draw_grid_lines(batch, card, grid);

    const text_style& header = find_text_style(text, style_scorecard_header);
    const std::array<const char*, 4> keys{{text_scorecard_header_player, text_scorecard_header_thru, text_scorecard_header_score,
                                           text_scorecard_header_relative}};
    for (std::size_t column = 0; column < keys.size(); ++column) {
        draw_label(batch, text.font, header, lookup_text(text, keys[column]), grid_cell(grid, 0, column));
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const group_scorecard_row& row = rows[i];
        const int grid_row = static_cast<int>(i) + 1;
        const text_style& ink = find_text_style(text, row.me ? style_scorecard_row_current : style_scorecard_row);
        draw_label(batch, text.font, with_align(ink, text_align::left), row.name, hole_cell(grid_cell(grid, grid_row, 0)));
        draw_label(batch, text.font, ink, std::to_string(row.holes_played), grid_cell(grid, grid_row, 1));
        draw_label(batch, text.font, ink, row.holes_played > 0 ? std::to_string(row.strokes) : "", grid_cell(grid, grid_row, 2));
        draw_label(batch, text.font, ink, row.holes_played > 0 ? row.relative_label : "", grid_cell(grid, grid_row, 3));
    }
}

void draw_course_results(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.015f, 0.013f, 0.012f), 0.70f);
    draw_scorecard_card(batch, text, scorecard, ui_rect{glm::vec2(0.0f), glm::vec2(0.74f, 0.80f)}, false);
}
