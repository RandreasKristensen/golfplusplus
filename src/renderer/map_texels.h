#pragma once

// What every printed map of the course (the paper course map and the hole
// signs' maps) prints texel by texel, shared so both read alike: each row as
// runs of equal ink, and a pin's flag. GL-free.

#include "game/lighting_loader.h"

#include <array>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Calls paint(row, first column, length, ink) for each run of equal ink along
// every row of a `width` x `height` map; ink_at(column, row) is the ink of a
// texel, or none for paper.
template <class InkAt, class Paint>
void paint_ink_runs(const int width, const int height, const InkAt& ink_at, const Paint& paint) {
    for (int row = 0; row < height; ++row) {
        int run_start = 0;
        std::optional<glm::vec3> run_ink;
        for (int column = 0; column <= width; ++column) {
            const std::optional<glm::vec3> ink = column < width ? ink_at(column, row) : std::nullopt;
            if (column == width || ink != run_ink) {
                if (run_ink) {
                    paint(row, run_start, column - run_start, *run_ink);
                }
                run_start = column;
            }
            run_ink = ink;
        }
    }
}

// A block of whole texels: its lowest-numbered corner and size, in ink.
struct map_block {
    glm::ivec2 low{0};
    glm::ivec2 size{0};
    glm::vec3 ink{0.0f};
};

// A pin's flag: a 1x4 stick on the pin's texel and a 2x2 flag at its top to
// the right. `rows_up`: row numbers grow upwards (else downwards).
inline std::array<map_block, 2> map_flag_blocks(const glm::ivec2 pin, const bool rows_up, const map_inks& inks) {
    const int top = rows_up ? pin.y + 2 : pin.y - 3;
    return {map_block{rows_up ? pin : pin - glm::ivec2(0, 3), glm::ivec2(1, 4), inks.flagstick},
            map_block{glm::ivec2(pin.x + 1, top), glm::ivec2(2, 2), inks.flag}};
}
