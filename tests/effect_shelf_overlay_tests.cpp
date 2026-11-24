#include "doctest.h"

#include "game/picture_loader.h"
#include "renderer/effect_icons.h"
#include "renderer/effect_shelf_layout.h"
#include "renderer/effect_shelf_overlay.h"
#include "renderer/hud_boxes.h"
#include "renderer/hud_overlay.h"
#include "renderer/hud_panel.h"
#include "renderer/picture_size.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
// The UI grids of 4:3, 16:9 and 21:9 windows at every scene resolution preset.
std::vector<overlay_grid> grids() {
    const picture_style picture = *load_picture_style(asset_root());
    std::vector<overlay_grid> all;
    for (const int lines : picture.scene_lines) {
        for (const auto& [width, height] : std::vector<std::pair<int, int>>{{1440, 1080}, {1920, 1080}, {2560, 1080}}) {
            const picture_size size = picture_size_for_window(width, height, lines, picture.ui_lines);
            all.push_back(overlay_grid{size.ui_width, size.ui_height});
        }
    }
    return all;
}

// A point or box in UI pixels, y down from the top.
glm::vec2 pixels_of(const glm::vec2 clip, const overlay_grid& grid) {
    return glm::vec2((clip.x + 1.0f) * 0.5f * static_cast<float>(grid.width), (1.0f - clip.y) * 0.5f * static_cast<float>(grid.height));
}

struct pixel_rect {
    float left;
    float top;
    float right;
    float bottom;
};

pixel_rect pixel_rect_of(const ui_rect& rect, const overlay_grid& grid) {
    const glm::vec2 top_left = pixels_of(glm::vec2(rect_left(rect), rect_top(rect)), grid);
    const glm::vec2 bottom_right = pixels_of(glm::vec2(rect_right(rect), rect_bottom(rect)), grid);
    return pixel_rect{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
}

bool overlap(const pixel_rect& a, const pixel_rect& b) {
    constexpr float touching = 0.01f;  // boxes that only share an edge do not overlap
    return a.left < b.right - touching && b.left < a.right - touching && a.top < b.bottom - touching &&
        b.top < a.bottom - touching;
}

glm::vec2 clip_of(const float x, const float y, const overlay_grid& grid) {
    return glm::vec2(x * 2.0f / static_cast<float>(grid.width) - 1.0f, 1.0f - y * 2.0f / static_cast<float>(grid.height));
}

render_effect_square dazed_square() {
    render_effect_square square;
    square.icon = "dazed";
    square.harmful = true;
    square.seconds = "19";
    square.name = "DAZED";
    square.description = "HIT BY A BALL OR LIGHTNING. YOU WALK SLOWER UNTIL IT PASSES.";
    square.changes = {"-30% WALKING SPEED", "+10% ALL XP", "+25% CART TOP SPEED"};
    return square;
}

std::vector<render_effect_square> squares_of(const std::size_t count) {
    std::vector<render_effect_square> squares(count, dazed_square());
    for (std::size_t i = 0; i < count; ++i) {
        squares[i].harmful = i % 2 == 0;
        squares[i].seconds = std::to_string(99 - i);
    }
    return squares;
}
}

TEST_CASE("the shelf's squares are pixel squares in two rows left of the club label, newest leftmost in the top row") {
    for (const overlay_grid& grid : grids()) {
        const pixel_rect club = pixel_rect_of(club_label_box(), grid);
        const pixel_rect mode = pixel_rect_of(mode_label_box(), grid);
        for (std::size_t count = 1; count <= 40; ++count) {
            const effect_shelf_layout layout = layout_effect_shelf(count, grid);
            REQUIRE(!layout.squares.empty());
            REQUIRE(layout.squares.size() == layout.seconds_boxes.size());
            CHECK(layout.rows == 2U);
            CHECK(layout.per_row >= 4U);
            float lowest_seconds = club.top;
            for (std::size_t i = 0; i < layout.squares.size(); ++i) {
                const std::size_t row = i / layout.per_row;
                CHECK(row < 2U);
                const pixel_rect square = pixel_rect_of(layout.squares[i], grid);
                CHECK(std::abs((square.right - square.left) - static_cast<float>(effect_square_pixels)) < 0.01f);
                CHECK(std::abs((square.bottom - square.top) - static_cast<float>(effect_square_pixels)) < 0.01f);
                if (row == 0) {
                    CHECK(std::abs(square.top - club.top) < 0.01f);
                }
                CHECK(square.right < club.left);
                CHECK(!overlap(square, club));
                CHECK(!overlap(square, mode));
                CHECK(square.left > mode.right);
                if (i % layout.per_row != 0) {
                    const pixel_rect before = pixel_rect_of(layout.squares[i - 1], grid);
                    // Each next square in a row is to the right of the one before it: the newest is leftmost.
                    CHECK(std::abs((square.left - before.right) - static_cast<float>(effect_square_gap_pixels)) < 0.01f);
                    CHECK(std::abs(square.top - before.top) < 0.01f);
                }
                // Its seconds sit directly under it, inside its slot, clear of the next square's.
                const pixel_rect seconds = pixel_rect_of(layout.seconds_boxes[i], grid);
                lowest_seconds = std::max(lowest_seconds, seconds.bottom);
                CHECK(seconds.top >= square.bottom);
                CHECK(seconds.top - square.bottom < 2.0f);
                CHECK(std::abs((seconds.left + seconds.right) - (square.left + square.right)) < 0.01f);
                CHECK(!overlap(seconds, club));
                for (std::size_t other = 0; other < i; ++other) {
                    CHECK(!overlap(seconds, pixel_rect_of(layout.seconds_boxes[other], grid)));
                    CHECK(!overlap(square, pixel_rect_of(layout.seconds_boxes[other], grid)));
                }
            }
            // Each row ends next to the club label, the top row full before the second begins.
            const std::size_t first_row = std::min(layout.squares.size(), layout.per_row);
            const pixel_rect top_last = pixel_rect_of(layout.squares[first_row - 1], grid);
            CHECK(std::abs((club.left - top_last.right) - 3.0f) < 0.01f);
            if (layout.squares.size() > layout.per_row) {
                CHECK(first_row == layout.per_row);
                const pixel_rect last = pixel_rect_of(layout.squares.back(), grid);
                CHECK(std::abs((club.left - last.right) - 3.0f) < 0.01f);
                const pixel_rect first_of_second = pixel_rect_of(layout.squares[layout.per_row], grid);
                const pixel_rect first_of_first = pixel_rect_of(layout.squares[0], grid);
                CHECK(first_of_second.top > first_of_first.bottom);
                // Row 2's seconds end level with the club label's bottom.
                CHECK(std::abs(lowest_seconds - club.bottom) < 1.0f);
            }
        }
    }
}

TEST_CASE("sources beyond what fits give a +N square in the last slot") {
    for (const overlay_grid& grid : grids()) {
        std::size_t capacity = 0;
        for (std::size_t count = 1; count <= 60; ++count) {
            const effect_shelf_layout layout = layout_effect_shelf(count, grid);
            if (layout.hidden == 0) {
                CHECK(layout.shown == count);
                CHECK(layout.squares.size() == count);
                capacity = count;
            } else {
                CHECK(layout.squares.size() == capacity);
                CHECK(layout.shown == capacity - 1);
                CHECK(layout.hidden == count - layout.shown);
            }
        }
        const effect_shelf_layout any = layout_effect_shelf(1, grid);
        CHECK(capacity == any.per_row * 2U);
        CHECK(capacity < 60U);
        const effect_shelf_layout over = layout_effect_shelf(capacity + 1, grid);
        CHECK(over.squares.size() == capacity);
        CHECK(over.hidden == 2U);
    }
    CHECK(layout_effect_shelf(0, overlay_grid{640, 360}).squares.empty());
    CHECK(layout_effect_shelf(3, overlay_grid{0, 0}).squares.empty());
}

TEST_CASE("a point in a square finds its source, a point just outside finds none") {
    for (const overlay_grid& grid : grids()) {
        const effect_shelf_layout layout = layout_effect_shelf(2, grid);
        REQUIRE(layout.shown == 2U);
        for (std::size_t i = 0; i < layout.shown; ++i) {
            const pixel_rect square = pixel_rect_of(layout.squares[i], grid);
            CHECK(effect_square_at(layout, layout.squares[i].center) == std::optional<std::size_t>(i));
            CHECK(effect_square_at(layout, clip_of(square.left + 1.0f, square.top + 1.0f, grid)) == std::optional<std::size_t>(i));
            CHECK(effect_square_at(layout, clip_of(square.right - 1.0f, square.bottom - 1.0f, grid)) == std::optional<std::size_t>(i));
            CHECK(!effect_square_at(layout, clip_of(square.left - 1.0f, (square.top + square.bottom) * 0.5f, grid)));
            CHECK(!effect_square_at(layout, clip_of((square.left + square.right) * 0.5f, square.top - 1.0f, grid)));
            CHECK(!effect_square_at(layout, clip_of((square.left + square.right) * 0.5f, square.bottom + 1.0f, grid)));
        }
        // Over the club label is not over the shelf.
        CHECK(!effect_square_at(layout, club_label_box().center));
        // The +N square holds no source.
        const effect_shelf_layout full = layout_effect_shelf(40, grid);
        REQUIRE(full.hidden > 0U);
        CHECK(!effect_square_at(full, full.squares.back().center));
        CHECK(effect_square_at(full, full.squares.front().center) == std::optional<std::size_t>(0));
    }
}

TEST_CASE("the tooltip is inside the screen and clear of the hovered square wherever the cursor is") {
    const text_assets& text = shipped_text_assets();
    const render_effect_square square = dazed_square();
    for (const overlay_grid& grid : grids()) {
        const effect_shelf_layout layout = layout_effect_shelf(3, grid);
        for (std::size_t i = 0; i < layout.shown; ++i) {
            const pixel_rect hovered = pixel_rect_of(layout.squares[i], grid);
            for (const glm::vec2 cursor : {layout.squares[i].center, clip_of(hovered.left + 1.0f, hovered.top + 1.0f, grid),
                                           clip_of(hovered.right - 1.0f, hovered.bottom - 1.0f, grid)}) {
                const effect_tooltip tooltip = layout_effect_tooltip(text, square, layout.squares[i], cursor, grid);
                const pixel_rect panel = pixel_rect_of(tooltip.panel, grid);
                CHECK(panel.left >= 0.0f);
                CHECK(panel.top >= 0.0f);
                CHECK(panel.right <= static_cast<float>(grid.width));
                CHECK(panel.bottom <= static_cast<float>(grid.height));
                CHECK(!overlap(panel, hovered));
                CHECK(panel.top >= hovered.bottom);

                // Every text is whole and inside the panel.
                CHECK(!tooltip.name.truncated);
                CHECK(tooltip.name.lines.size() == 1U);
                CHECK(!tooltip.description.truncated);
                CHECK(tooltip.description.lines.size() >= 2U);
                REQUIRE(tooltip.changes.size() == square.changes.size());
                std::vector<const text_layout*> texts{&tooltip.name, &tooltip.description};
                for (const text_layout& change : tooltip.changes) {
                    CHECK(!change.truncated);
                    CHECK(change.lines.size() == 1U);
                    texts.push_back(&change);
                }
                for (const text_layout* each : texts) {
                    const pixel_rect box = pixel_rect_of(each->bounds, grid);
                    CHECK(box.left >= panel.left);
                    CHECK(box.right <= panel.right);
                    CHECK(box.top >= panel.top);
                    CHECK(box.bottom <= panel.bottom);
                }
            }
        }
    }
}

TEST_CASE("an item's tooltip has no description and just a name and its changes") {
    const text_assets& text = shipped_text_assets();
    render_effect_square item = dazed_square();
    item.description.clear();
    const overlay_grid grid{640, 360};
    const effect_shelf_layout layout = layout_effect_shelf(1, grid);
    const effect_tooltip tooltip = layout_effect_tooltip(text, item, layout.squares[0], layout.squares[0].center, grid);
    CHECK(tooltip.description.lines.empty());
    CHECK(tooltip.name.lines.size() == 1U);
    CHECK(tooltip.changes.size() == 3U);
}

TEST_CASE("the shelf draws without cutting text off, and the hovered square's tooltip over it") {
    const text_assets& text = shipped_text_assets();
    for (const overlay_grid& grid : grids()) {
        for (const std::size_t count : {std::size_t{1}, std::size_t{3}, std::size_t{12}}) {
            const std::vector<render_effect_square> squares = squares_of(count);
            overlay_batch plain;
            plain.grid = grid;
            draw_effect_shelf(plain, text, squares, std::nullopt);
            draw_effect_tooltip(plain, text, squares, std::nullopt);
            CHECK(plain.truncated_text_count == 0U);
            CHECK(overlay_batch_quad_count(plain) > 0U);

            const effect_shelf_layout layout = layout_effect_shelf(count, grid);
            overlay_batch hovered;
            hovered.grid = grid;
            draw_effect_shelf(hovered, text, squares, layout.squares[0].center);
            const std::size_t shelf_quads = overlay_batch_quad_count(hovered);
            draw_effect_tooltip(hovered, text, squares, layout.squares[0].center);
            CHECK(hovered.truncated_text_count == 0U);
            CHECK(overlay_batch_quad_count(hovered) > shelf_quads);
            // A tooltip only with the cursor over a square.
            overlay_batch away;
            away.grid = grid;
            draw_effect_tooltip(away, text, squares, club_label_box().center);
            CHECK(overlay_batch_quad_count(away) == 0U);

            // Through the whole HUD too.
            render_data data;
            data.effect_squares = squares;
            data.effect_cursor = layout.squares[0].center;
            overlay_batch hud;
            hud.grid = grid;
            draw_hud(hud, text, data, glm::mat4(1.0f));
            CHECK(hud.truncated_text_count == 0U);
        }
    }
}

TEST_CASE("a debuff's border is the error red and a buff's the HUD's amber") {
    const text_assets& text = shipped_text_assets();
    const overlay_grid grid{640, 360};
    const auto has_colour = [](const overlay_batch& batch, const glm::vec3& colour) {
        return std::any_of(batch.vertices.begin(), batch.vertices.end(),
                           [&colour](const overlay_vertex& vertex) { return glm::vec3(vertex.color) == colour; });
    };
    render_effect_square buff = dazed_square();
    buff.harmful = false;
    overlay_batch buff_batch;
    buff_batch.grid = grid;
    draw_effect_shelf(buff_batch, text, {buff}, std::nullopt);
    CHECK(has_colour(buff_batch, hud_amber));
    CHECK(!has_colour(buff_batch, find_text_style(text, "error").color));

    overlay_batch debuff_batch;
    debuff_batch.grid = grid;
    draw_effect_shelf(debuff_batch, text, {dazed_square()}, std::nullopt);
    CHECK(has_colour(debuff_batch, find_text_style(text, "error").color));
    CHECK(!has_colour(debuff_batch, hud_amber));
}

TEST_CASE("every shipped event effect names an icon the renderer draws, and so does every item kind") {
    CHECK(!shipped_content().effects.effects.empty());
    for (const auto& [id, effect] : shipped_content().effects.effects) {
        CHECK(effect_icon_known(effect.icon));
    }
    for (const item_kind kind : {item_kind::smokable, item_kind::drinkable, item_kind::cart, item_kind::trinket, item_kind::club,
                                 item_kind::bag}) {
        CHECK(effect_icon_known(item_kind_name(kind)));
    }
    CHECK(!effect_icon_known("no_such_icon"));
    CHECK(!effect_icon_known(""));
}

TEST_CASE("an icon fills its box with cells and an unknown one draws nothing") {
    const ui_rect box{glm::vec2(0.2f, 0.3f), glm::vec2(0.05f, 0.07f)};
    for (const char* icon : {"dazed", "smokable", "drinkable", "cart", "plain"}) {
        overlay_batch batch;
        draw_effect_icon(batch, icon, box);
        CHECK(overlay_batch_quad_count(batch) > 0U);
        for (const overlay_vertex& vertex : batch.vertices) {
            CHECK(vertex.position.x >= rect_left(box) - 0.0001f);
            CHECK(vertex.position.x <= rect_right(box) + 0.0001f);
            CHECK(vertex.position.y >= rect_bottom(box) - 0.0001f);
            CHECK(vertex.position.y <= rect_top(box) + 0.0001f);
        }
    }
    overlay_batch none;
    draw_effect_icon(none, "no_such_icon", box);
    CHECK(overlay_batch_quad_count(none) == 0U);
}
