#include "doctest.h"

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <glm/common.hpp>

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
const overlay_grid wide_grid{640, 360};

pixel_font_data test_font() {
    const std::optional<text_assets> text = load_text_assets(GOLFPP_ASSETS_DIR);
    CHECK(text.has_value());
    return text ? text->font : pixel_font_data{};
}

text_style plain_style() {
    text_style style;
    style.fill = 1.0f;
    style.width_fill = 1.0f;
    style.min_scale = 1;
    style.color = glm::vec3(1.0f);
    return style;
}

overlay_batch batch_on(const overlay_grid grid) {
    overlay_batch batch;
    batch.grid = grid;
    return batch;
}

int lit_pixels(const pixel_font_data& font, const std::u32string& text) {
    int lit = 0;
    for (const char32_t c : text) {
        lit += find_glyph(font, c).lit_pixel_count;
    }
    return lit;
}

// Quad corners in target pixels (x right, y down from the top).
glm::vec2 to_pixels(const glm::vec2 clip, const overlay_grid& grid) {
    return glm::vec2((clip.x + 1.0f) * 0.5f * static_cast<float>(grid.width),
                     (1.0f - clip.y) * 0.5f * static_cast<float>(grid.height));
}

// Every vertex lies inside `box`, give or take half a target pixel of grid
// snapping.
bool inside(const overlay_batch& batch, const ui_rect& box) {
    const glm::vec2 slack(1.0f / static_cast<float>(batch.grid.width), 1.0f / static_cast<float>(batch.grid.height));
    for (const overlay_vertex& vertex : batch.vertices) {
        if (vertex.position.x < rect_left(box) - slack.x || vertex.position.x > rect_right(box) + slack.x ||
            vertex.position.y < rect_bottom(box) - slack.y || vertex.position.y > rect_top(box) + slack.y) {
            return false;
        }
    }
    return true;
}
}

TEST_CASE("ui rects slice into rows and columns") {
    const ui_rect box = rect_from_edges(-0.5f, -0.2f, 0.5f, 0.2f);
    CHECK(std::abs(rect_left(box) + 0.5f) < 1e-6f);
    CHECK(std::abs(rect_top(box) - 0.2f) < 1e-6f);

    const ui_rect top_row = rect_row(box, 0, 4);
    CHECK(std::abs(rect_top(top_row) - 0.2f) < 1e-6f);
    CHECK(std::abs(rect_bottom(top_row) - 0.1f) < 1e-6f);

    const ui_rect right_quarter = slice_x(box, 0.75f, 1.0f);
    CHECK(std::abs(rect_left(right_quarter) - 0.25f) < 1e-6f);
    CHECK(std::abs(rect_right(right_quarter) - 0.5f) < 1e-6f);

    CHECK(inset_rect(box, glm::vec2(1.0f)).half_size == glm::vec2(0.0f));
}

TEST_CASE("a label draws one quad per lit font pixel, lowercase as uppercase") {
    const pixel_font_data font = test_font();
    const ui_rect box{glm::vec2(0.0f), glm::vec2(0.9f, 0.5f)};
    text_style style = plain_style();
    style.max_scale = 1;
    const std::vector<std::pair<std::string, std::u32string>> labels{
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", U"ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {"abcdefghijklmnopqrstuvwxyz", U"ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {"0123456789+-/:_.,!?()'#@", U"0123456789+-/:_.,!?()'#@"},
        {u8"æøå", U"ÆØÅ"},
    };
    for (const auto& label : labels) {
        overlay_batch batch = batch_on(wide_grid);
        draw_label(batch, font, style, label.first, box);
        CHECK(overlay_batch_quad_count(batch) == static_cast<std::size_t>(lit_pixels(font, label.second)));
        CHECK(batch.truncated_text_count == 0U);
    }
}

TEST_CASE("font pixels are whole, square blocks of target pixels") {
    const pixel_font_data font = test_font();
    for (const overlay_grid grid : {overlay_grid{640, 360}, overlay_grid{554, 416}, overlay_grid{733, 314}}) {
        for (const int scale : {1, 2, 3}) {
            overlay_batch batch = batch_on(grid);
            const text_layout layout = layout_text_at_scale(font, plain_style(), "AØ", ui_rect{glm::vec2(0.013f, -0.021f), glm::vec2(0.8f)}, grid, scale);
            draw_text_layout(batch, font, layout, glm::vec3(1.0f));
            CHECK(overlay_batch_quad_count(batch) == static_cast<std::size_t>(lit_pixels(font, U"AØ")));
            for (std::size_t quad = 0; quad < overlay_batch_quad_count(batch); ++quad) {
                const glm::vec2 a = to_pixels(batch.vertices[quad * overlay_vertices_per_quad].position, grid);
                const glm::vec2 b = to_pixels(batch.vertices[quad * overlay_vertices_per_quad + 2].position, grid);
                CHECK(std::abs(a.x - std::round(a.x)) < 1e-3f);
                CHECK(std::abs(a.y - std::round(a.y)) < 1e-3f);
                CHECK(std::abs(std::abs(b.x - a.x) - static_cast<float>(scale)) < 1e-3f);
                CHECK(std::abs(std::abs(b.y - a.y) - static_cast<float>(scale)) < 1e-3f);
            }
        }
    }
}

TEST_CASE("fill picks the scale from the box height, capped by max_scale") {
    const pixel_font_data font = test_font();
    // 0.1 clip units of a 360 pixel tall target is 18 pixels: two font
    // heights of 7 fit, three would not.
    const ui_rect box{glm::vec2(0.0f), glm::vec2(0.9f, 0.05f)};
    text_style style = plain_style();
    CHECK(fit_text_scale(font, style, "HI", box, wide_grid) == 2);

    style.fill = 0.4f;  // 7.2 pixels wanted
    CHECK(fit_text_scale(font, style, "HI", box, wide_grid) == 1);

    const ui_rect tall{glm::vec2(0.0f), glm::vec2(0.9f, 0.5f)};
    style.fill = 1.0f;
    CHECK(fit_text_scale(font, style, "HI", tall, wide_grid) > 3);
    style.max_scale = 3;
    CHECK(fit_text_scale(font, style, "HI", tall, wide_grid) == 3);
}

TEST_CASE("text shrinks to fit the width, then is cut off with the ellipsis") {
    const pixel_font_data font = test_font();
    const ui_rect box{glm::vec2(0.0f), glm::vec2(0.14f, 0.05f)};  // 89.6 x 18 pixels
    const text_style style = plain_style();

    // 48 font pixels wide: 96 target pixels at scale 2 is too wide.
    const text_layout fits = layout_text(font, style, "ABCDEFGHIJ", box, wide_grid);
    CHECK(fits.scale == 1);
    CHECK(!fits.truncated);

    const std::string too_long = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const text_layout cut = layout_text(font, style, too_long, box, wide_grid);
    CHECK(cut.truncated);
    REQUIRE(cut.lines.size() == 1U);
    CHECK(cut.lines[0].glyphs.size() < too_long.size());
    CHECK(cut.lines[0].glyphs.substr(cut.lines[0].glyphs.size() - font.ellipsis.size()) == font.ellipsis);

    overlay_batch batch = batch_on(wide_grid);
    draw_text_layout(batch, font, cut, glm::vec3(1.0f));
    CHECK(batch.truncated_text_count == 1U);
    CHECK(inside(batch, box));
}

TEST_CASE("wrapping styles break between words before shrinking") {
    const pixel_font_data font = test_font();
    const ui_rect box{glm::vec2(0.0f), glm::vec2(0.15f, 0.12f)};  // 96 x 43 pixels
    text_style style = plain_style();
    style.wrap = true;

    const text_layout layout = layout_text(font, style, "PITCHING WEDGE", box, wide_grid);
    CHECK(layout.scale == 2);
    CHECK(!layout.truncated);
    REQUIRE(layout.lines.size() == 2U);
    CHECK(layout.lines[0].glyphs == U"PITCHING");
    CHECK(layout.lines[1].glyphs == U"WEDGE");

    style.wrap = false;
    CHECK(layout_text(font, style, "PITCHING WEDGE", box, wide_grid).scale == 1);
}

TEST_CASE("explicit line breaks stack lines a line gap apart") {
    const pixel_font_data font = test_font();
    const text_layout layout = layout_text_at_scale(font, plain_style(), "A\nB", ui_rect{glm::vec2(0.0f), glm::vec2(0.5f)}, wide_grid, 2);
    REQUIRE(layout.lines.size() == 2U);
    const float step_pixels = (layout.lines[0].top_left.y - layout.lines[1].top_left.y) * 0.5f * 360.0f;
    CHECK(std::abs(step_pixels - static_cast<float>((font.height + font.line_gap) * 2)) < 1e-3f);
}

TEST_CASE("lines that don't fit the height are dropped and the last is cut off") {
    const pixel_font_data font = test_font();
    const ui_rect box{glm::vec2(0.0f), glm::vec2(0.5f, 0.03f)};  // 10.8 pixels: one line
    const text_layout layout = layout_text(font, plain_style(), "ONE\nTWO\nTHREE", box, wide_grid);
    CHECK(layout.truncated);
    REQUIRE(layout.lines.size() == 1U);
    CHECK(layout.lines[0].glyphs == U"ONE" + font.ellipsis);
}

TEST_CASE("alignment places text inside the box margins") {
    const pixel_font_data font = test_font();
    const ui_rect box{glm::vec2(0.1f, 0.2f), glm::vec2(0.5f, 0.1f)};
    text_style style = plain_style();
    style.max_scale = 2;

    style.align = text_align::left;
    const text_layout left = layout_text(font, style, "I", box, wide_grid);
    CHECK(std::abs(rect_left(left.bounds) - rect_left(box)) <= 1.0f / 640.0f + 1e-6f);

    style.align = text_align::right;
    const text_layout right = layout_text(font, style, "I", box, wide_grid);
    CHECK(std::abs(rect_right(right.bounds) - rect_right(box)) <= 1.0f / 640.0f + 1e-6f);

    style.align = text_align::center;
    style.valign = text_valign::center;
    const text_layout centered = layout_text(font, style, "I", box, wide_grid);
    CHECK(std::abs(centered.bounds.center.x - box.center.x) <= 1.0f / 640.0f + 1e-6f);
    CHECK(std::abs(centered.bounds.center.y - box.center.y) <= 1.0f / 360.0f + 1e-6f);

    style.valign = text_valign::top;
    const text_layout top = layout_text(font, style, "I", box, wide_grid);
    CHECK(std::abs(rect_top(top.bounds) - rect_top(box)) <= 1.0f / 360.0f + 1e-6f);
}

TEST_CASE("text stays inside its box on every aspect ratio") {
    const pixel_font_data font = test_font();
    const std::vector<std::string> labels{"", "A", "HOLE 18 THE VERY LONG HOLE NAME", u8"ÆØÅ ÆBLEGRØD", "1\n2\n3\n4\n5\n6", "     "};
    const std::vector<ui_rect> boxes{ui_rect{glm::vec2(0.0f), glm::vec2(0.9f, 0.5f)},
                                     ui_rect{glm::vec2(-0.6f, 0.7f), glm::vec2(0.12f, 0.04f)},
                                     ui_rect{glm::vec2(0.3f, -0.4f), glm::vec2(0.05f, 0.2f)},
                                     ui_rect{glm::vec2(0.0f), glm::vec2(0.01f)}};
    for (const overlay_grid grid : {overlay_grid{640, 360}, overlay_grid{554, 416}, overlay_grid{733, 314}, overlay_grid{120, 120}}) {
        for (const bool wrap : {false, true}) {
            text_style style = plain_style();
            style.wrap = wrap;
            for (const ui_rect& box : boxes) {
                for (const std::string& label : labels) {
                    overlay_batch batch = batch_on(grid);
                    draw_label(batch, font, style, label, box);
                    CHECK(inside(batch, box));
                }
            }
        }
    }
}

TEST_CASE("nothing is drawn without a pixel grid") {
    const pixel_font_data font = test_font();
    overlay_batch batch;
    draw_label(batch, font, plain_style(), "ABC", ui_rect{glm::vec2(0.0f), glm::vec2(0.5f)});
    CHECK(batch.vertices.empty());
}
