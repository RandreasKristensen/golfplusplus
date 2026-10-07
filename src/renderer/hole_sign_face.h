#pragma once

// What is painted on a hole sign's face (game/hole_sign.h): the hole number,
// a top-down map of the hole in the course map's inks (course_map_fill.h),
// its par and its length. The map shows only the hole's own ribbon and zones,
// turned so the hole runs across the landscape face: tee on the right and
// pin on the left, the way the hole lies past the sign for someone reading
// it from the tee. Laid out with the overlay primitives and the pixel font on
// the face's own texel grid, then painted into a small texture once per
// course (overlay_raster.h), so its texels stay chunky in the scene.
// GL-free.

#include <optional>
#include <string>

#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/text_assets.h"
#include "renderer/overlay_batch.h"
#include "renderer/ui_rect.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// The face texture's size in texels (the board's width and height, so its
// aspect is the board's: hole_sign tuning).
inline constexpr int hole_sign_face_width = 96;
inline constexpr int hole_sign_face_height = 64;
// The map's box on the face, in texels from the face's top-left corner.
inline constexpr int hole_sign_map_left = 5;
inline constexpr int hole_sign_map_top = 16;
inline constexpr int hole_sign_map_width = 86;
inline constexpr int hole_sign_map_height = 34;

// The board's wood, behind the painted panel and on its back, ends and posts.
inline const glm::vec3 hole_sign_wood(0.30f, 0.19f, 0.10f);

struct hole_sign_labels {
    std::string number;  // "HOLE 3"
    std::string par;     // "PAR 4"
    std::string length;  // "385M", in whole metres like the rangefinder
};

hole_sign_labels make_hole_sign_labels(const text_assets& text, int hole_number, int par, int meters);

// The box `width` x `height` texels whose top-left texel is (`left`, `top`),
// counted from the face's top-left corner, in the face's clip space.
ui_rect hole_sign_texels(int left, int top, int width, int height);

// How the hole lies in the map box: its tee-to-pin line across it, the tee
// on the right, fitted by the hole's own extent (its ribbon, greens and
// bunkers; water may run off the edge) with a margin. Map texel (column,
// row) counts from the box's top-left corner.
struct hole_sign_map_frame {
    glm::vec3 origin{0.0f};                   // the world point at the box centre
    glm::vec3 to_pin{0.0f, 0.0f, 1.0f};       // towards the map's left
    glm::vec3 far_side{-1.0f, 0.0f, 0.0f};    // the hole's right: towards the map's top
    float texels_per_unit = 1.0f;

    glm::vec3 world(float column, float row) const;
    glm::ivec2 texel(const glm::vec3& point) const;
};

hole_sign_map_frame make_hole_sign_map_frame(const play_area& area, const hole_sign& sign);

// What the map shows at `point`: the hole's own zone there, else its
// ribbon's fairway or rough, nothing off the hole.
std::optional<terrain_material> hole_sign_map_material(const play_area& area, const hole_sign& sign, const glm::vec3& point);

// Sets `batch`'s grid to the face and appends the face over the wood: the
// painted panel, the labels and the map of `sign`'s hole (its ribbon in
// `area`, its zones).
void draw_hole_sign_face(overlay_batch& batch,
                         const text_assets& text,
                         const hole_sign_labels& labels,
                         const play_area& area,
                         const hole_sign& sign);
