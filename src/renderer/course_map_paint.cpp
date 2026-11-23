#include "renderer/course_map_sheet.h"

#include "renderer/map_ink.h"
#include "renderer/map_texels.h"
#include "renderer/overlay_batch.h"
#include "renderer/overlay_raster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace {
// Marks in texels: how thick a fence is, and how far apart the dots of a
// cart path lie along it.
constexpr float fence_thickness = 1.0f;
constexpr float cart_dot_spacing = 3.0f;
constexpr float cart_dot_size = 1.1f;

// What a texel of the trees' layer prints.
enum class tree_texel : std::uint8_t { none, shadow, canopy, outline };

// The trees as a printed course guide draws them, texel by texel: every
// shadow first, offset to the lower right (rows grow upwards); then every
// crown, so overlapping ones merge into a wood; then a one-texel outline
// round the whole mass, scalloped where its crowns are.
class tree_layer {
public:
    explicit tree_layer(const course_map_sheet& sheet)
        : width_(sheet.width), height_(sheet.height), texels_(static_cast<std::size_t>(sheet.width * sheet.height), tree_texel::none) {
        for (const sheet_tree& tree : sheet.trees) {
            disc(tree.center + glm::vec2(1.0f, -1.0f), tree.radius + 1.0f, tree_texel::shadow, false);
        }
        for (const sheet_tree& tree : sheet.trees) {
            disc(tree.center, tree.radius, tree_texel::canopy, true);
        }
        // A texel the crowns surround on three sides is canopy too, so a wood has no pinholes.
        std::vector<glm::ivec2> pits;
        for (int row = 0; row < height_; ++row) {
            for (int column = 0; column < width_; ++column) {
                const glm::ivec2 texel(column, row);
                if (at(texel) != tree_texel::canopy && canopy_neighbours(texel) >= 3) {
                    pits.push_back(texel);
                }
            }
        }
        for (const glm::ivec2 texel : pits) {
            texels_[index(texel)] = tree_texel::canopy;
        }
        for (int row = 0; row < height_; ++row) {
            for (int column = 0; column < width_; ++column) {
                const glm::ivec2 texel(column, row);
                if (at(texel) != tree_texel::canopy && canopy_neighbours(texel) > 0) {
                    texels_[index(texel)] = tree_texel::outline;
                }
            }
        }
    }

    tree_texel at(const glm::ivec2 texel) const {
        return texel.x >= 0 && texel.y >= 0 && texel.x < width_ && texel.y < height_ ? texels_[index(texel)] : tree_texel::none;
    }

private:
    int canopy_neighbours(const glm::ivec2 texel) const {
        int count = 0;
        for (const glm::ivec2 step : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
            count += at(texel + step) == tree_texel::canopy ? 1 : 0;
        }
        return count;
    }

    std::size_t index(const glm::ivec2 texel) const {
        return static_cast<std::size_t>(texel.y * width_ + texel.x);
    }

    // The texels whose middles lie within `radius` of `center`, kept where
    // nothing is yet unless `over`.
    void disc(const glm::vec2 center, const float radius, const tree_texel ink, const bool over) {
        const int low_row = std::max(0, static_cast<int>(std::floor(center.y - radius)));
        const int high_row = std::min(height_ - 1, static_cast<int>(std::ceil(center.y + radius)));
        const int low_column = std::max(0, static_cast<int>(std::floor(center.x - radius)));
        const int high_column = std::min(width_ - 1, static_cast<int>(std::ceil(center.x + radius)));
        for (int row = low_row; row <= high_row; ++row) {
            for (int column = low_column; column <= high_column; ++column) {
                const glm::vec2 offset = glm::vec2(static_cast<float>(column) + 0.5f, static_cast<float>(row) + 0.5f) - center;
                tree_texel& texel = texels_[index(glm::ivec2(column, row))];
                if (glm::dot(offset, offset) <= radius * radius && (over || texel == tree_texel::none)) {
                    texel = ink;
                }
            }
        }
    }

    int width_;
    int height_;
    std::vector<tree_texel> texels_;
};

class sheet_painter {
public:
    sheet_painter(overlay_batch& batch, const course_map_sheet& sheet) : batch_(batch), sheet_(sheet) {}

    // A rectangle of whole texels.
    void block(const glm::ivec2 low, const glm::ivec2 size, const glm::vec3 color) {
        const glm::vec2 half = glm::vec2(size) * 0.5f;
        rect(glm::vec2(low) + half, half, color);
    }

    void rect(const glm::vec2 center, const glm::vec2 half, const glm::vec3 color) {
        draw_overlay_quad(batch_, clip(center), half * texel_clip(), color);
    }

    // A line of `thickness` texels, its ends square.
    void stroke(const glm::vec2 from, const glm::vec2 to, const float thickness, const glm::vec3 color) {
        const glm::vec2 delta = to - from;
        const float length = glm::length(delta);
        if (length <= 0.0001f) {
            return;
        }
        const glm::vec2 side = glm::vec2(-delta.y, delta.x) / length * (thickness * 0.5f);
        draw_overlay_corner_quad(batch_, clip(from - side), clip(from + side), clip(to + side), clip(to - side), color, 1.0f);
    }

    void polyline(const std::vector<glm::vec2>& corners, const float thickness, const glm::vec3 color) {
        for (std::size_t i = 0; i + 1 < corners.size(); ++i) {
            stroke(corners[i], corners[i + 1], thickness, color);
        }
    }

    // A dot every `spacing` texels along the line.
    void dots(const std::vector<glm::vec2>& corners, const float spacing, const float size, const glm::vec3 color) {
        float next = 0.0f;
        for (std::size_t i = 0; i + 1 < corners.size(); ++i) {
            const glm::vec2 delta = corners[i + 1] - corners[i];
            const float length = glm::length(delta);
            for (; next < length; next += spacing) {
                rect(corners[i] + delta * (next / length), glm::vec2(size * 0.5f), color);
            }
            next -= length;
        }
    }

private:
    // Clip units across one texel.
    glm::vec2 texel_clip() const {
        return glm::vec2(2.0f / static_cast<float>(sheet_.width), 2.0f / static_cast<float>(sheet_.height));
    }
    glm::vec2 clip(const glm::vec2 texel) const {
        return glm::vec2(-1.0f + 2.0f * texel.x / static_cast<float>(sheet_.width),
                         -1.0f + 2.0f * texel.y / static_cast<float>(sheet_.height));
    }

    overlay_batch& batch_;
    const course_map_sheet& sheet_;
};
}

rgba_image paint_course_map_sheet(const course_map_sheet& sheet, const map_inks& inks) {
    overlay_batch batch;
    batch.grid = overlay_grid{sheet.width, sheet.height};
    sheet_painter paint(batch, sheet);

    paint_ink_runs(
        sheet.width, sheet.height,
        [&](const int column, const int row) -> std::optional<glm::vec3> {
            const std::optional<terrain_material> material = sheet_material(sheet, glm::ivec2(column, row));
            return material ? std::optional<glm::vec3>(map_material_ink(inks, *material)) : std::nullopt;
        },
        [&](const int row, const int start, const int length, const glm::vec3 ink) {
            paint.block(glm::ivec2(start, row), glm::ivec2(length, 1), ink);
        });
    for (const std::vector<glm::vec2>& path : sheet.cart_paths) {
        paint.dots(path, cart_dot_spacing, cart_dot_size, inks.cart_path);
    }
    for (const std::vector<glm::vec2>& fence : sheet.fences) {
        paint.polyline(fence, fence_thickness, inks.fence);
    }
    const tree_layer trees(sheet);
    paint_ink_runs(
        sheet.width, sheet.height,
        [&](const int column, const int row) -> std::optional<glm::vec3> {
            switch (trees.at(glm::ivec2(column, row))) {
                case tree_texel::shadow: return inks.tree_shadow;
                case tree_texel::canopy: return inks.tree;
                case tree_texel::outline: return inks.tree_outline;
                case tree_texel::none: break;
            }
            return std::nullopt;
        },
        [&](const int row, const int start, const int length, const glm::vec3 ink) {
            paint.block(glm::ivec2(start, row), glm::ivec2(length, 1), ink);
        });
    for (const glm::ivec2 pin : sheet.pins) {
        for (const map_block& flag : map_flag_blocks(pin, true, inks)) {
            paint.block(flag.low, flag.size, flag.ink);
        }
    }

    rgba_image image = rasterize_overlay_batch(batch, inks.paper);
    for (std::size_t texel = 0; texel < sheet.ground.size(); ++texel) {
        if (sheet.ground[texel] == sheet_no_ground) {
            image.pixels[texel * 4 + 3] = 0;
        }
    }
    return image;
}
