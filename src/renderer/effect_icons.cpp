#include "renderer/effect_icons.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace {
using icon_rows = std::array<const char*, effect_icon_cells>;

// Each character is a colour of the icon's palette; '.' is clear.
struct icon_art {
    const char* name;
    icon_rows rows;
    std::vector<std::pair<char, glm::vec3>> palette;
};

const std::vector<icon_art>& icons() {
    static const std::vector<icon_art> art{
        // A spiral: dizzy.
        {"dazed",
         {{".########.", "#........#", "#.######.#", "#.#....#.#", "#.#.##.#.#", "#.#.#..#.#", "#.#.####.#", "#.#......#",
           "#.########", "#........."}},
         {{'#', glm::vec3(0.95f, 0.84f, 0.36f)}}},
        // A cigarette with a wisp of smoke: filter, paper, ember.
        {"smokable",
         {{".....s....", "......s...", ".....s....", "......s...", ".....s....", "..........", "ffwwwwwwee", "ffwwwwwwee",
           "ffwwwwwwee", ".........."}},
         {{'s', glm::vec3(0.62f, 0.64f, 0.62f)},
          {'f', glm::vec3(0.86f, 0.60f, 0.26f)},
          {'w', glm::vec3(0.92f, 0.91f, 0.86f)},
          {'e', glm::vec3(0.92f, 0.36f, 0.16f)}}},
        // A can: rims, body, a highlight down its side.
        {"drinkable",
         {{".mmmmmmmm.", ".gggggggg.", ".hggggggg.", ".hggggggg.", ".hggggggg.", ".hggggggg.", ".hggggggg.", ".hggggggg.",
           ".gggggggg.", ".mmmmmmmm."}},
         {{'m', glm::vec3(0.76f, 0.78f, 0.78f)},
          {'g', glm::vec3(0.88f, 0.66f, 0.18f)},
          {'h', glm::vec3(1.0f, 0.90f, 0.62f)}}},
        // A diamond, for any other item.
        {"plain",
         {{"....##....", "...####...", "..######..", ".########.", "##########", "##########", ".########.", "..######..",
           "...####...", "....##...."}},
         {{'#', glm::vec3(0.70f, 0.78f, 0.92f)}}},
    };
    return art;
}

// What an item kind that has no art of its own is drawn as.
constexpr const char* plain_icon = "plain";
constexpr std::array<const char*, 4> plain_kinds{{"cart", "trinket", "club", "bag"}};

const icon_art* find_icon(const std::string& name) {
    for (const char* kind : plain_kinds) {
        if (name == kind) {
            return find_icon(plain_icon);
        }
    }
    for (const icon_art& art : icons()) {
        if (name == art.name) {
            return &art;
        }
    }
    return nullptr;
}

const glm::vec3* colour_of(const icon_art& art, const char mark) {
    for (const auto& [key, colour] : art.palette) {
        if (key == mark) {
            return &colour;
        }
    }
    return nullptr;
}
}

bool effect_icon_known(const std::string& icon) {
    return find_icon(icon) != nullptr;
}

void draw_effect_icon(overlay_batch& batch, const std::string& icon, const ui_rect& box) {
    const icon_art* art = find_icon(icon);
    if (art == nullptr) {
        return;
    }
    const glm::vec2 cell = box.half_size * 2.0f / static_cast<float>(effect_icon_cells);
    const glm::vec2 top_left(rect_left(box), rect_top(box));
    for (std::size_t y = 0; y < art->rows.size(); ++y) {
        std::size_t x = 0;
        while (x < effect_icon_cells) {
            const char mark = art->rows[y][x];
            std::size_t end = x + 1;
            while (end < effect_icon_cells && art->rows[y][end] == mark) {
                ++end;
            }
            if (const glm::vec3* colour = colour_of(*art, mark)) {
                const float run = static_cast<float>(end - x);
                const glm::vec2 center = top_left + glm::vec2((static_cast<float>(x) + run * 0.5f) * cell.x,
                                                              -(static_cast<float>(y) + 0.5f) * cell.y);
                draw_overlay_quad(batch, center, glm::vec2(run * cell.x, cell.y) * 0.5f, *colour);
            }
            x = end;
        }
    }
}
