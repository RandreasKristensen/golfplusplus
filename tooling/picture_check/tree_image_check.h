#pragma once

// Trees drawn as their baked images against the same trees drawn whole, at the
// crown size an image is first allowed (lighting.json "trees"."impostor"):
// silhouette displacement, coverage, luminance and depth, over views between
// the grid's own, every species and variant, seasons and noon and night.
#include "check_support.h"

enum class tree_image_scope { gate, full };
bool check_tree_images(const lighting_palette& palette, const std::string& root, tree_image_scope scope);
