#include "renderer/osd_overlay.h"

#include "game/text_ids.h"
#include "renderer/hud_overlay.h"
#include "renderer/hud_panel.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

namespace {
const glm::vec3 osd_white(0.97f, 0.97f, 0.94f);
const glm::vec3 osd_black(0.0f);
const glm::vec3 overswing_warning(0.92f, 0.22f, 0.14f);
// A target when the batch has none: the picture's usual size.
constexpr overlay_grid fallback_grid{320, 240};
// A segment reaching further off screen than this (clip units) is not drawn.
constexpr float far_off_screen = 4.0f;
// The landing bracket's half width, its arms' drop and its gap over the
// landing; the rest tick's half height. In target pixels.
constexpr int bracket_half_width = 5;
constexpr int bracket_drop = 2;
constexpr int bracket_rise = 3;
constexpr int tick_half_height = 2;

overlay_grid grid_of(const overlay_batch& batch) {
    return batch.grid.width > 0 && batch.grid.height > 0 ? batch.grid : fallback_grid;
}

glm::ivec2 to_pixel(const overlay_grid grid, const glm::vec2 clip) {
    return glm::ivec2(static_cast<int>(std::floor((clip.x + 1.0f) * 0.5f * static_cast<float>(grid.width))),
                      static_cast<int>(std::floor((clip.y + 1.0f) * 0.5f * static_cast<float>(grid.height))));
}

// Every target pixel of the stepped line from `a` to `b` (Bresenham).
std::vector<glm::ivec2> line_pixels(const glm::ivec2 a, const glm::ivec2 b) {
    std::vector<glm::ivec2> pixels;
    const int dx = std::abs(b.x - a.x);
    const int dy = -std::abs(b.y - a.y);
    const int sx = a.x < b.x ? 1 : -1;
    const int sy = a.y < b.y ? 1 : -1;
    int error = dx + dy;
    glm::ivec2 at = a;
    while (true) {
        pixels.push_back(at);
        if (at == b) {
            break;
        }
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            at.x += sx;
        }
        if (twice <= dx) {
            error += dx;
            at.y += sy;
        }
    }
    return pixels;
}

// Edge first, then the line over it, so the edge never covers the line.
void draw_pixels(overlay_batch& batch, const std::vector<glm::ivec2>& pixels) {
    const overlay_grid grid = grid_of(batch);
    const glm::vec2 pixel(2.0f / static_cast<float>(grid.width), 2.0f / static_cast<float>(grid.height));
    const auto centre = [&](const glm::ivec2 at) {
        return glm::vec2((static_cast<float>(at.x) + 0.5f) * pixel.x - 1.0f, (static_cast<float>(at.y) + 0.5f) * pixel.y - 1.0f);
    };
    const auto on_target = [&](const glm::ivec2 at) {
        return at.x >= -1 && at.y >= -1 && at.x <= grid.width && at.y <= grid.height;
    };
    for (const glm::ivec2 at : pixels) {
        if (on_target(at)) {
            draw_overlay_quad(batch, centre(at), pixel * 1.5f, osd_black, 1.0f);
        }
    }
    for (const glm::ivec2 at : pixels) {
        if (on_target(at)) {
            draw_overlay_quad(batch, centre(at), pixel * 0.5f, osd_white, 1.0f);
        }
    }
}

// The swing clock: a quarter of a clock face whose hand is the club, from
// address at 6 round to the top of the backswing at 12 (the golfer's clock
// system, its half circle folded into a quarter), past 12 into overswing.
// Round on the screen at any aspect ratio.
struct swing_clock {
    glm::vec2 pivot;
    float radius;   // clip y
    float x_per_y;  // clip x per clip y for the same number of pixels

    // `fraction` 0 is 6 o'clock (pointing left), 1 is 12 (up), past 1 right of it.
    glm::vec2 point(const float fraction, const float radius_share) const {
        const float angle = glm::pi<float>() * (1.0f - 0.5f * fraction);
        return pivot + glm::vec2(std::cos(angle) * x_per_y, std::sin(angle)) * (radius * radius_share);
    }
};

// A ring between two radius shares from fraction `from` to `to` (an inner
// share of 0 is a pie wedge).
void draw_clock_ring(overlay_batch& batch,
                     const swing_clock& clock,
                     const float inner,
                     const float outer,
                     const float from,
                     const float to,
                     const glm::vec3 color,
                     const float alpha) {
    constexpr float quarter_segments = 24.0f;
    const int segments = std::max(1, static_cast<int>(std::ceil((to - from) * quarter_segments)));
    for (int i = 0; i < segments; ++i) {
        const float a = from + (to - from) * static_cast<float>(i) / static_cast<float>(segments);
        const float b = from + (to - from) * static_cast<float>(i + 1) / static_cast<float>(segments);
        draw_overlay_corner_quad(batch, clock.point(a, inner), clock.point(a, outer), clock.point(b, outer), clock.point(b, inner),
                                 color, alpha);
    }
}
}

void draw_osd_line(overlay_batch& batch, const glm::vec2 a, const glm::vec2 b) {
    if (glm::any(glm::greaterThan(glm::abs(a), glm::vec2(far_off_screen))) ||
        glm::any(glm::greaterThan(glm::abs(b), glm::vec2(far_off_screen)))) {
        return;
    }
    const overlay_grid grid = grid_of(batch);
    draw_pixels(batch, line_pixels(to_pixel(grid, a), to_pixel(grid, b)));
}

void draw_world_segments(overlay_batch& batch, const std::vector<std::pair<glm::vec3, glm::vec3>>& segments,
                         const glm::mat4& view_proj) {
    for (const auto& [from, to] : segments) {
        const std::optional<glm::vec2> a = project_to_screen(view_proj, from);
        const std::optional<glm::vec2> b = project_to_screen(view_proj, to);
        if (a && b) {
            draw_osd_line(batch, *a, *b);
        }
    }
}

void draw_shot_preview(overlay_batch& batch, const render_data& data, const glm::mat4& view_proj) {
    std::vector<glm::ivec2> pixels;
    const overlay_grid grid = grid_of(batch);
    std::optional<glm::vec2> previous;
    for (const glm::vec3& point : data.osd_line) {
        const std::optional<glm::vec2> projected = project_to_screen(view_proj, point);
        if (projected && previous && glm::all(glm::lessThanEqual(glm::abs(*projected), glm::vec2(far_off_screen))) &&
            glm::all(glm::lessThanEqual(glm::abs(*previous), glm::vec2(far_off_screen)))) {
            const std::vector<glm::ivec2> step = line_pixels(to_pixel(grid, *previous), to_pixel(grid, *projected));
            pixels.insert(pixels.end(), step.begin(), step.end());
        }
        previous = projected;
    }
    // The landing bracket, its arms down either side over where the ball lands.
    if (data.osd_landing) {
        if (const std::optional<glm::vec2> landing = project_to_screen(view_proj, *data.osd_landing)) {
            const glm::ivec2 at = to_pixel(grid, *landing) + glm::ivec2(0, bracket_rise);
            for (const int side : {-1, 1}) {
                const glm::ivec2 outer = at + glm::ivec2(side * bracket_half_width, 0);
                const std::vector<glm::ivec2> bar = line_pixels(at + glm::ivec2(side * 2, 0), outer);
                const std::vector<glm::ivec2> arm = line_pixels(outer, outer - glm::ivec2(0, bracket_drop));
                pixels.insert(pixels.end(), bar.begin(), bar.end());
                pixels.insert(pixels.end(), arm.begin(), arm.end());
            }
        }
    }
    // The rest tick, upright where it stops.
    if (data.osd_rest) {
        if (const std::optional<glm::vec2> rest = project_to_screen(view_proj, *data.osd_rest)) {
            const glm::ivec2 at = to_pixel(grid, *rest);
            const std::vector<glm::ivec2> tick =
                line_pixels(at - glm::ivec2(0, tick_half_height), at + glm::ivec2(0, tick_half_height));
            pixels.insert(pixels.end(), tick.begin(), tick.end());
        }
    }
    draw_pixels(batch, pixels);
}

void draw_setup_word(overlay_batch& batch, const text_assets& text, const std::string& word) {
    if (word.empty()) {
        return;
    }
    const text_style& style = with_align(find_text_style(text, style_datestamp), text_align::right);
    draw_label(batch, text.font, style, word, rect_from_edges(0.40f, -0.34f, 0.95f, -0.24f));
}

void draw_swing_clock(overlay_batch& batch, const text_assets& text, const float backswing, const float backswing_max) {
    const float power = std::clamp(backswing, 0.0f, std::max(1.0f, backswing_max));
    const float x_per_y = batch.grid.width > 0 ? static_cast<float>(batch.grid.height) / static_cast<float>(batch.grid.width) : 1.0f;
    const swing_clock clock{glm::vec2(0.89f, -0.84f), 0.46f, x_per_y};
    const swing_clock shadow{clock.pivot + glm::vec2(0.012f, -0.014f), clock.radius, x_per_y};
    constexpr float line = 0.008f;
    const float overswing_end = std::max(1.0f, backswing_max);

    draw_clock_ring(batch, shadow, 0.0f, 1.0f, 0.0f, 1.0f, glm::vec3(0.0f), 0.22f);
    draw_clock_ring(batch, clock, 0.0f, 1.0f, 0.0f, 1.0f, hud_panel_color, 0.80f);
    draw_clock_ring(batch, clock, 1.0f - line / clock.radius, 1.0f, 0.0f, 1.0f, hud_outline_color, 0.54f);
    const glm::vec2 six_end = clock.point(0.0f, 1.0f);
    const glm::vec2 twelve_end = clock.point(1.0f, 1.0f);
    draw_overlay_quad(batch, glm::vec2((six_end.x + clock.pivot.x) * 0.5f, clock.pivot.y),
                      glm::vec2((clock.pivot.x - six_end.x) * 0.5f, line * 0.5f), hud_outline_color, 0.54f);
    draw_overlay_quad(batch, glm::vec2(clock.pivot.x, (twelve_end.y + clock.pivot.y) * 0.5f),
                      glm::vec2(line * 0.5f * x_per_y, (twelve_end.y - clock.pivot.y) * 0.5f), hud_outline_color, 0.54f);

    draw_clock_ring(batch, clock, 0.64f, 0.76f, 0.0f, 1.0f, glm::vec3(0.030f, 0.032f, 0.034f), 0.92f);
    if (overswing_end > 1.0f) {
        // Past 12, in a warning tone: more speed, much wider dice.
        draw_clock_ring(batch, clock, 0.64f, 0.76f, 1.0f, overswing_end, overswing_warning, 0.55f);
    }
    if (power > 0.0f) {
        draw_clock_ring(batch, clock, 0.67f, 0.73f, 0.0f, std::min(power, 1.0f), hud_amber, 0.92f);
    }
    if (power > 1.0f) {
        draw_clock_ring(batch, clock, 0.67f, 0.73f, 1.0f, power, overswing_warning, 0.95f);
    }

    const text_style& hours = find_text_style(text, style_hud_scale);
    const glm::vec2 hour_half(0.05f * x_per_y, 0.034f);
    const std::array<std::pair<float, const char*>, 3> numbered{
        {{0.07f, text_hud_swing_six}, {0.5f, text_hud_swing_nine}, {0.92f, text_hud_swing_twelve}}};
    for (const auto& [fraction, key] : numbered) {
        draw_label(batch, text.font, hours, lookup_text(text, key), ui_rect{clock.point(fraction, 0.88f), hour_half});
    }

    // The club: its shaft from the pivot, its head on the track, trailing
    // towards 6 (or leading, at address, so it stays on the dial).
    const glm::vec3 club_color(0.98f, 0.80f, 0.24f);
    constexpr float head_share = 0.07f;
    const float head_from = std::max(0.0f, power - head_share);
    draw_overlay_segment(batch, clock.pivot, clock.point(power, 0.74f), 0.010f, club_color, 0.96f);
    draw_clock_ring(batch, clock, 0.63f, 0.77f, head_from, head_from + head_share, club_color, 0.96f);
}

void draw_bag_pictogram(overlay_batch& batch, const bag_pictogram place) {
    // In target pixels from its middle, left of the club label: a bag (a
    // tall bar with club heads over it) on a cart's wheels, slung on a
    // figure's back, or stood on a ground line.
    const overlay_grid grid = grid_of(batch);
    const glm::ivec2 middle = to_pixel(grid, glm::vec2(0.54f, 0.76f));
    std::vector<glm::ivec2> pixels;
    const auto stroke = [&](const glm::ivec2 a, const glm::ivec2 b) {
        const std::vector<glm::ivec2> line = line_pixels(middle + a, middle + b);
        pixels.insert(pixels.end(), line.begin(), line.end());
    };
    switch (place) {
    case bag_pictogram::cart:
        stroke({-6, -2}, {6, -2});
        stroke({-6, -2}, {-6, 1});
        stroke({6, -2}, {6, 1});
        stroke({-4, -4}, {-4, -4});
        stroke({4, -4}, {4, -4});
        stroke({0, -1}, {0, 5});
        stroke({-1, 6}, {1, 6});
        break;
    case bag_pictogram::back:
        stroke({-2, 6}, {-2, 6});
        stroke({-2, 4}, {-2, -1});
        stroke({-2, -1}, {-4, -5});
        stroke({-2, -1}, {0, -5});
        stroke({1, -1}, {3, 5});
        stroke({2, 6}, {4, 6});
        break;
    case bag_pictogram::ground:
        stroke({-5, -5}, {5, -5});
        stroke({0, -4}, {1, 3});
        stroke({0, 4}, {2, 4});
        break;
    }
    draw_pixels(batch, pixels);
}
